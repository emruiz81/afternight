// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointF>
#include <QSize>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTimeZone>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "processes/ImageIntegration/core/CometGuidedAutoTracker.h"
#include "processes/ImageIntegration/core/CometRegistrationProductBridge.h"
#include "processes/ImageIntegration/core/CometStackingProductResolver.h"
#include "processes/ImageIntegration/core/CometReferenceResolutionService.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/persistence/RegistrationMetricsRepository.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/core/RegistrationMetricsPersistenceTypes.h"
#include "processes/core/RegistrationMetricsStateLoader.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/core/StandaloneRegistrationPersistence.h"

#include "Test_ImageIntegration/ImportSession.h"
#include "Test_ImageIntegration/TempDirFixture.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>
#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

namespace
{
using an::ui::RegisteredFrame;
using an::ui::RegisteredFrameSet;
using an::ui::RegisteredStar;

QString metricsDbPath(const QString& root)
{
    return QDir(root).filePath(QStringLiteral("registration_metrics.db"));
}

/// The stars a registration recorded for a frame, at their engine pixel coordinates.
std::vector<RegisteredStar> makeStars(std::initializer_list<QPointF> positions)
{
    std::vector<RegisteredStar> stars;
    stars.reserve(positions.size());
    for (const auto& position : positions)
    {
        stars.push_back(RegisteredStar{.x = position.x(), .y = position.y(), .fwhm = 2.0, .roundness = 0.9});
    }
    return stars;
}

/// A translation onto the reference, as a projective transform.
an::RegistrationTransform makeTranslationTransform(double dx, double dy)
{
    an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Projective,
                                         .sourceToReference = {1.0, 0.0, dx, 0.0, 1.0, dy, 0.0, 0.0, 1.0}});
    EXPECT_TRUE(transform.hasValue()) << transform.status().message();
    return transform.hasValue() ? *transform : an::RegistrationTransform{};
}

/// A second-order distortion about a reference pixel, with one linear term in each forward component.
an::SipDistortion makeSimpleSip()
{
    an::SipDistortionRequest request;
    request.referencePixel = an::PixelCoordinate{512.0, 256.0};
    request.forwardX.order = 2u;
    request.forwardX.coefficients[1][0] = 0.001;
    request.forwardY.order = 2u;
    request.forwardY.coefficients[0][1] = -0.0015;
    an::Result<an::SipDistortion> sip = an::SipDistortion::create(request);
    EXPECT_TRUE(sip.hasValue()) << sip.status().message();
    return sip.hasValue() ? *sip : an::SipDistortion{};
}

/// A light as the integration's import describes it: its size, its target and the acquisition the group key reads.
FrameInfo makeLightFrame(const QString& path, const int width, const int height)
{
    return FrameInfo(FrameId(path), path,
                     FrameMetadata(FrameType::Light, QSize(width, height), 60.0, std::nullopt, 100, 10,
                                   QStringLiteral("L"), QStringLiteral("M42"),
                                   QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString()));
}

const RegistrationMetricsCandidate* findCandidate(const std::vector<RegistrationMetricsCandidate>& candidates,
                                                  const QString& frameId)
{
    const auto canonical = FrameId(frameId).canonicalPath();
    const auto it =
        std::find_if(candidates.begin(), candidates.end(), [&](const RegistrationMetricsCandidate& candidate) {
            return candidate.id.canonicalPath() == canonical;
        });
    return (it != candidates.end()) ? &(*it) : nullptr;
}

const an::ui::RegistrationMetricRecord* findMetricRecord(const std::vector<an::ui::RegistrationMetricRecord>& metrics,
                                                         const QString& frameId)
{
    const auto canonical = FrameId(frameId).canonicalPath();
    const auto it = std::find_if(metrics.begin(), metrics.end(), [&](const an::ui::RegistrationMetricRecord& record) {
        return record.frameId.canonicalPath() == canonical;
    });
    return (it != metrics.end()) ? &(*it) : nullptr;
}

/// The lookup statuses a spectrophotometric calibration records against a matched source.
constexpr char kLookupSatisfied[] = "satisfied";
constexpr char kLookupStaleMatch[] = "stale_match";

an::MatchedSourceReference makeMatchedSourceReference(std::uint64_t sourceId)
{
    an::MatchedSourceReference reference;
    reference.sourceIdentifier = sourceId;
    reference.astrometricCatalogFamilyId = "gaia_dr3";
    reference.astrometricCatalogManifestId = "gaia-dr3-main-2026-05";
    reference.spectrumCatalogFamilyId = "spcc_gaia_dr3_xp_sampled";
    reference.spectrumCatalogManifestId = "spcc-gaia-dr3-xp-sampled-fixture-v1";
    reference.wcsSolutionId = "iip-wcs-solution-42";
    reference.observationEpoch = "2025.25";
    reference.properMotionModelVersion = "gaia-dr3-pm-v1";
    reference.separationArcseconds = 0.37;
    reference.residualPixels = 0.21;
    return reference;
}

/// A matched source as a registration records it for one frame, with the lookup status a calibration gave it.
an::ui::RegistrationMatchedSourceRecord matchedSourceRecord(const FrameId& frameId, const QString& groupKey,
                                                            const int settingsVersion,
                                                            const an::MatchedSourceReference& reference,
                                                            const char* lookupStatus, const qint64 updatedAt)
{
    an::ui::RegistrationMatchedSourceRecord record;
    record.frameId = frameId;
    record.groupKey = groupKey;
    record.settingsVersion = settingsVersion;
    record.reference = reference;
    record.lookupStatus = QString::fromLatin1(lookupStatus);
    record.updatedAt = updatedAt;
    return record;
}

const an::ui::RegistrationMatchedSourceRecord*
findMatchedSourceRecord(const std::vector<an::ui::RegistrationMatchedSourceRecord>& records, const QString& frameId,
                        const std::uint64_t sourceId)
{
    const auto canonical = FrameId(frameId).canonicalPath();
    const auto it =
        std::find_if(records.begin(), records.end(), [&](const an::ui::RegistrationMatchedSourceRecord& record) {
            return record.frameId.canonicalPath() == canonical && record.reference.sourceIdentifier == sourceId;
        });
    return (it != records.end()) ? &(*it) : nullptr;
}

int tableRowCount(QSqlDatabase& db, const QString& tableName)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(1) FROM %1").arg(tableName)) || !query.next())
    {
        return -1;
    }
    return query.value(0).toInt();
}

bool insertDuplicateCometTransform(QSqlDatabase& db, const an::ui::CometTransformRoleRecord& record)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO comet_transform_roles ("
                                 "comet_run_id, frame_id, role, comet_output_plane_id, transform_json, "
                                 "source_distortion_json, succeeded, failure_reason, updated_at) "
                                 "VALUES (:comet_run_id, :frame_id, :role, :comet_output_plane_id, :transform_json, "
                                 ":source_distortion_json, :succeeded, :failure_reason, :updated_at)"));
    query.bindValue(QStringLiteral(":comet_run_id"), record.cometRunId);
    query.bindValue(QStringLiteral(":frame_id"), record.frameId.canonicalPath());
    query.bindValue(QStringLiteral(":role"), record.role);
    query.bindValue(QStringLiteral(":comet_output_plane_id"), record.cometOutputPlaneId);
    query.bindValue(QStringLiteral(":transform_json"), record.transformJson);
    query.bindValue(QStringLiteral(":source_distortion_json"), record.sourceDistortionJson);
    query.bindValue(QStringLiteral(":succeeded"), record.succeeded ? 1 : 0);
    query.bindValue(QStringLiteral(":failure_reason"), record.failureReason);
    query.bindValue(QStringLiteral(":updated_at"), record.updatedAt);
    return query.exec();
}

/// Where the tracking cases' frames sit in time: seconds since the Unix epoch, before each frame's own offset.
constexpr double kTrackingEpochBase = 1700000000.0;

[[nodiscard]] an::ui::CometGuidedAutoTrackingFrame makeGuidedTrackingFrame(const QString& frameId, double epoch)
{
    an::ui::CometGuidedAutoTrackingFrame frame;
    frame.context.originalFrameId = FrameId(frameId);
    frame.context.refinementSourcePath = frameId;
    frame.context.midpointEpoch = kTrackingEpochBase + epoch;
    frame.context.midpoint = an::ui::cometInstantOfEpochSeconds(kTrackingEpochBase + epoch);
    frame.context.status = an::ui::CometTrackingFrameContextStatus::Ready;
    return frame;
}

/// One operation of `session`, which the tracker's fit and its refinements run under.
[[nodiscard]] an::Result<an::consumer::OperationScope>
admitTrackingOperation(const an::consumer::EngineSession& session)
{
    return session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
}

/// A grey Float32 frame of a flat sky at one, with a Gaussian nucleus whose brightest pixel holds the engine pixel
/// coordinate `nucleus` when one is given.
[[nodiscard]] an::Result<an::Image<float>> makeGuidedTrackingImage(const an::consumer::EngineSession& session,
                                                                   const std::optional<an::PixelCoordinate> nucleus)
{
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kWidth, kHeight, 1u, hold->engine().limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the engine refused the tracking frame's extent");
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the engine refused a grey Float32 description");
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, 1.0f);
    if (!image.hasValue() || !nucleus.has_value())
    {
        return image;
    }

    const std::optional<an::ImageWriteAccess<float>> access = image->write();
    if (!access.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the tracking frame holds nothing to write");
    }
    constexpr int kRadius = 5;
    constexpr double kSigma = 1.2;
    const int peakX = static_cast<int>(std::floor(nucleus->x));
    const int peakY = static_cast<int>(std::floor(nucleus->y));
    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            const int x = peakX + dx;
            const int y = peakY + dy;
            if (x < 0 || y < 0 || x >= static_cast<int>(kWidth) || y >= static_cast<int>(kHeight))
            {
                continue;
            }
            const std::optional<std::span<float>> row = access->view().row(0u, static_cast<std::uint32_t>(y));
            if (!row.has_value())
            {
                return an::Status::failure(an::ErrorCode::OutOfRange, "the tracking frame has no such row");
            }
            const double value =
                1.0 + 120.0 * std::exp(-(static_cast<double>(dx * dx + dy * dy)) / (2.0 * kSigma * kSigma));
            (*row)[static_cast<std::size_t>(x)] = static_cast<float>(value);
        }
    }
    return image;
}

/// A refiner that measures the nucleus on `image` for the frame `frameId`, as the registration stage measures a frame
/// it read, and has no image for any other frame.
[[nodiscard]] an::ui::CometGuidedRefiner imageRefiner(const an::ExecutionContext& execution,
                                                      const an::Image<float>& image, const FrameId& frameId)
{
    return [&execution, &image, frameId](
               const an::ui::CometTrackingFrameContext& context, const an::PixelCoordinate& seed,
               const an::CometCenterRefinementOptions& options) -> an::Result<an::ui::CometRefinementMeasurement> {
        if (!(context.originalFrameId == frameId))
        {
            return an::Status::failure(an::ErrorCode::InvalidData, "the stand-in holds no image of this frame");
        }
        const std::optional<an::ImageView<const float>> pixels = image.view();
        const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
        if (!pixels.has_value() || !descriptor.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the refinement image holds nothing");
        }
        const an::ImageLayout layout = pixels->layout();
        const an::Result<an::StarFrameView> view =
            an::StarFrameView::create(pixels->data(), layout.totalBytes(), layout, *descriptor);
        if (!view.hasValue())
        {
            return view.status();
        }
        const an::Result<an::CometCenterRefinementResult> refined =
            an::refineCometCenter(execution, *view, seed, options);
        if (!refined.hasValue())
        {
            return refined.status();
        }
        return an::ui::CometRefinementMeasurement{
            .refined = *refined, .width = descriptor->extent().width(), .height = descriptor->extent().height()};
    };
}

[[nodiscard]] const an::ui::CometGuidedAutoTrackingDiagnostic*
findGuidedTrackingDiagnostic(const an::ui::CometGuidedAutoTrackingResult& result, const QString& frameId)
{
    const FrameId id(frameId);
    const auto it = std::find_if(
        result.diagnostics.begin(), result.diagnostics.end(),
        [&](const an::ui::CometGuidedAutoTrackingDiagnostic& diagnostic) { return diagnostic.originalFrameId == id; });
    return it == result.diagnostics.end() ? nullptr : &(*it);
}

QString loadCometRunStatus(const QString& dbPath, const QString& runId)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("TestCometRunStatusReader"));
    if (!connection.open(&error))
    {
        return QString();
    }

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("SELECT status FROM comet_tracking_runs WHERE comet_run_id = :comet_run_id"));
    query.bindValue(QStringLiteral(":comet_run_id"), runId);
    if (!query.exec() || !query.next())
    {
        return QString();
    }

    return query.value(0).toString();
}

int cometRunCountForSettingsVersion(const QString& dbPath, const QString& groupKey, const int settingsVersion)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("TestCometRunVersionCountReader"));
    if (!connection.open(&error))
    {
        return -1;
    }

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("SELECT COUNT(1) FROM comet_tracking_runs "
                                 "WHERE group_key = :group_key AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    if (!query.exec() || !query.next())
    {
        return -1;
    }

    return query.value(0).toInt();
}

an::ui::CometTrackingRunRecord makeCometRun(const QString& runId, const QString& groupKey,
                                            const FrameId& referenceFrame, const QString& status)
{
    an::ui::CometTrackingRunRecord run;
    run.cometRunId = runId;
    run.groupKey = groupKey;
    run.settingsVersion = 4;
    run.cometContextId = QStringLiteral("ctx-lifecycle");
    run.detectionHash = QStringLiteral("det-lifecycle");
    run.registrationHash = QStringLiteral("reg-lifecycle");
    run.lightFilesHash = QStringLiteral("lights-lifecycle");
    run.calibrationHash = QStringLiteral("cal-lifecycle");
    run.resolvedReferenceFrameId = referenceFrame.canonicalPath();
    run.mode = QStringLiteral("PureComet");
    run.status = status;
    return run;
}

an::ui::CometOutputPlaneRecord makeCometPlane(const QString& runId)
{
    an::ui::CometOutputPlaneRecord plane;
    plane.cometOutputPlaneId = QStringLiteral("plane-%1").arg(runId);
    plane.cometRunId = runId;
    plane.role = QStringLiteral("comet_aligned");
    plane.alignmentMode = QStringLiteral("PureComet");
    plane.coordinateFrameKind = QStringLiteral("CometReference");
    plane.canvasWidth = 256;
    plane.canvasHeight = 256;
    plane.skyWcsValid = false;
    plane.skyWcsInvalidReason = QStringLiteral("non-sidereal");
    return plane;
}

an::ui::CometRoleReadinessRecord makeCometReadiness(const QString& runId, const QString& readinessStatus,
                                                    const bool requiresMaterializedOutputs,
                                                    const int materializedOutputCount = -1)
{
    an::ui::CometRoleReadinessRecord readiness;
    readiness.cometRunId = runId;
    readiness.role = QStringLiteral("comet_aligned");
    readiness.readinessStatus = readinessStatus;
    readiness.selectedFrameCount = 2;
    readiness.successfulTransformCount = 2;
    readiness.outputPlaneStatus = QStringLiteral("Ready");
    readiness.requiresMaterializedOutputs = requiresMaterializedOutputs;
    readiness.materializedOutputCount =
        (materializedOutputCount >= 0) ? materializedOutputCount : (requiresMaterializedOutputs ? 2 : 0);
    return readiness;
}

an::ui::RegistrationMetricRecord makeReferenceMetric(const FrameId& frameId, const QString& groupKey,
                                                     const int settingsVersion, const int stars, const double fwhm,
                                                     const double snr, const bool succeeded = true)
{
    an::ui::RegistrationMetricRecord metric;
    metric.frameId = frameId;
    metric.groupKey = groupKey;
    metric.settingsVersion = settingsVersion;
    metric.metrics.stars = stars;
    metric.metrics.fwhm = fwhm;
    metric.metrics.snr = snr;
    metric.metrics.eccentricity = 0.1;
    metric.metrics.photometric_zero_point = 21.0;
    metric.metrics.zero_point_scatter = 0.1;
    metric.metrics.astrometric_rms_pixels = 1.0;
    metric.metrics.astrometric_rms_arcsec = 0.4;
    metric.metrics.limiting_magnitude = 18.0;
    metric.metrics.catalog_matched_stars = stars / 2;
    metric.metrics.succeeded = succeeded;
    metric.outputPath = QStringLiteral("aligned/%1.fit").arg(QFileInfo(frameId.canonicalPath()).baseName());
    metric.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(0.0, 0.0));
    return metric;
}

void writePlaceholderFile(const QString& path)
{
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("placeholder"), qint64(11));
}

an::ui::StackingCometArtifactRecord makeStackingCometArtifact(const QString& cometRunId,
                                                              const QString& cometOutputPlaneId,
                                                              const QString& artifactKind, const QString& outputPath,
                                                              const QString& artifactHash)
{
    an::ui::StackingCometArtifactRecord artifact;
    artifact.cometRunId = cometRunId;
    artifact.integrationGroupKey = QStringLiteral("integration|stars-comet|artifact-contract");
    artifact.stackingHash = QStringLiteral("stacking-stars-comet-contract");
    artifact.starBaseOutputDescriptorId = QStringLiteral("base-star-plane-contract");
    artifact.cometOutputPlaneId = cometOutputPlaneId;
    artifact.artifactKind = artifactKind;
    artifact.outputPath = outputPath;
    artifact.artifactHash = artifactHash;
    artifact.maskSettingsFingerprint = QStringLiteral("mask-contract");
    artifact.succeeded = true;
    return artifact;
}

an::ui::RegistrationGroupState makeGroupStateForCometRun(const an::ui::CometTrackingRunRecord& run)
{
    an::ui::RegistrationGroupState state;
    state.groupKey = run.groupKey;
    state.settingsVersion = run.settingsVersion;
    state.detectionHash = run.detectionHash;
    state.registrationHash = run.registrationHash;
    state.lightFilesHash = run.lightFilesHash;
    state.calibrationHash = run.calibrationHash;
    state.referenceFrameId = run.resolvedReferenceFrameId;
    state.outputWcsJson = QStringLiteral("{\"schemaVersion\":1}");
    return state;
}

an::ui::CometRegisteredOutputRecord makeCometRegisteredOutput(const an::ui::CometTrackingRunRecord& run,
                                                              const an::ui::CometOutputPlaneRecord& plane,
                                                              const FrameId& frame, const QString& outputPath,
                                                              const QString& artifactHash)
{
    an::ui::CometRegisteredOutputRecord output;
    output.cometRunId = run.cometRunId;
    output.frameId = frame;
    output.role = QStringLiteral("comet_aligned");
    output.cometOutputPlaneId = plane.cometOutputPlaneId;
    output.outputPath = outputPath;
    output.artifactHash = artifactHash;
    output.succeeded = true;
    return output;
}

an::ui::RegistrationStarCacheEntry makeBatchStarCacheEntry(const FrameId& frameId, const QString& groupKey,
                                                           const QString& detectionHash, const int ordinal)
{
    an::ui::RegistrationStarCacheEntry entry;
    entry.frameId = frameId;
    entry.groupKey = groupKey;
    entry.detectionHash = detectionHash;
    entry.inputPath = frameId.canonicalPath();
    entry.inputMtime = 1700010000 + ordinal;
    entry.starsCount = 100 + ordinal;
    entry.fwhm = 1.5 + static_cast<double>(ordinal) * 0.1;
    entry.snr = 20.0 + static_cast<double>(ordinal);
    entry.roundness = 0.8;
    entry.starsBlob = an::ui::serializeRegistrationStarCacheBlob(makeStars({QPointF(10.0 + ordinal, 20.0 + ordinal)}));
    entry.updatedAt = 1700020000 + ordinal;
    return entry;
}

QString sqliteQuotedLiteral(QString value)
{
    value.replace('\'', QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

void installFrameAbortTrigger(const QString& dbPath, const QString& triggerName, const QString& event,
                              const QString& tableName, const QString& canonicalFrameId, const QString& message)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("GroupTriggerInstaller"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    QSqlQuery query(connection.db());
    const QString sql =
        QStringLiteral("CREATE TRIGGER %1 BEFORE %2 ON %3 WHEN NEW.frame_id = %4 "
                       "BEGIN SELECT RAISE(ABORT, %5); END")
            .arg(triggerName, event, tableName, sqliteQuotedLiteral(canonicalFrameId), sqliteQuotedLiteral(message));
    ASSERT_TRUE(query.exec(sql)) << query.lastError().text().toStdString();
}

void replaceStoredMetricFrameId(const QString& dbPath, const QString& groupKey, const int settingsVersion,
                                const QString& currentFrameId, const QString& storedFrameId)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("AliasMetricKeyReplacer"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("UPDATE registration_metrics SET frame_id = :stored_frame_id "
                                 "WHERE frame_id = :current_frame_id AND group_key = :group_key "
                                 "AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":stored_frame_id"), storedFrameId);
    query.bindValue(QStringLiteral(":current_frame_id"), currentFrameId);
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();
    ASSERT_EQ(query.numRowsAffected(), 1);
}

void replaceStoredStarCacheFrameId(const QString& dbPath, const QString& groupKey, const QString& detectionHash,
                                   const QString& currentFrameId, const QString& storedFrameId)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("IdentAliasStarCacheKeyReplacer"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("UPDATE star_detection_cache SET frame_id = :stored_frame_id "
                                 "WHERE frame_id = :current_frame_id AND group_key = :group_key "
                                 "AND detection_hash = :detection_hash"));
    query.bindValue(QStringLiteral(":stored_frame_id"), storedFrameId);
    query.bindValue(QStringLiteral(":current_frame_id"), currentFrameId);
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":detection_hash"), detectionHash);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();
    ASSERT_EQ(query.numRowsAffected(), 1);
}

void replaceStoredMatchedSourceFrameId(const QString& dbPath, const QString& groupKey, const int settingsVersion,
                                       const QString& sourceId, const QString& currentFrameId,
                                       const QString& storedFrameId)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("IdentAliasMatchedSourceKeyReplacer"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("UPDATE registration_matched_sources SET frame_id = :stored_frame_id "
                                 "WHERE frame_id = :current_frame_id AND group_key = :group_key "
                                 "AND settings_version = :settings_version AND gaia_source_id = :source_id"));
    query.bindValue(QStringLiteral(":stored_frame_id"), storedFrameId);
    query.bindValue(QStringLiteral(":current_frame_id"), currentFrameId);
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    query.bindValue(QStringLiteral(":source_id"), sourceId);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();
    ASSERT_EQ(query.numRowsAffected(), 1);
}

void duplicateStoredMetricFrameId(const QString& dbPath, const QString& groupKey, const int settingsVersion,
                                  const QString& sourceFrameId, const QString& duplicateFrameId,
                                  const QString& duplicateOutputPath, const qint64 updatedAtOffset = 0)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("AliasMetricKeyDuplicator"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral(
        "INSERT INTO registration_metrics (frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
        "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, astrometric_rms_arcsec, "
        "limiting_magnitude, catalog_matched_stars, succeeded, output_path, transform_json, updated_at) "
        "SELECT :duplicate_frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
        "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, astrometric_rms_arcsec, "
        "limiting_magnitude, catalog_matched_stars, succeeded, :duplicate_output_path, transform_json, "
        "updated_at + :updated_at_offset "
        "FROM registration_metrics WHERE frame_id = :source_frame_id AND group_key = :group_key "
        "AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":duplicate_frame_id"), duplicateFrameId);
    query.bindValue(QStringLiteral(":duplicate_output_path"), duplicateOutputPath);
    query.bindValue(QStringLiteral(":updated_at_offset"), updatedAtOffset);
    query.bindValue(QStringLiteral(":source_frame_id"), sourceFrameId);
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();
    ASSERT_EQ(query.numRowsAffected(), 1);
}

void updateStoredMetricStars(const QString& dbPath, const QString& groupKey, const int settingsVersion,
                             const QString& storedFrameId, const int stars)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("AliasMetricStarsUpdater"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("UPDATE registration_metrics SET stars = :stars "
                                 "WHERE frame_id = :frame_id AND group_key = :group_key "
                                 "AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":stars"), stars);
    query.bindValue(QStringLiteral(":frame_id"), storedFrameId);
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();
    ASSERT_EQ(query.numRowsAffected(), 1);
}

QString storedMetricOutputPath(const QString& dbPath, const QString& groupKey, const int settingsVersion,
                               const QString& storedFrameId)
{
    QString error;
    core::SqliteConnection connection(dbPath, QStringLiteral("AliasMetricKeyReader"));
    if (!connection.open(&error))
    {
        return {};
    }

    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("SELECT output_path FROM registration_metrics "
                                 "WHERE frame_id = :frame_id AND group_key = :group_key "
                                 "AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":frame_id"), storedFrameId);
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    return query.exec() && query.next() ? query.value(0).toString() : QString();
}

void expectPersistenceCounterDelta(const WorkflowExecutionCounters& before, const WorkflowExecutionCounters& after,
                                   const std::uint64_t statementExecutions, const std::uint64_t committedTransactions,
                                   const std::uint64_t rolledBackTransactions)
{
    EXPECT_EQ(after.sqliteStatementExecutions() - before.sqliteStatementExecutions(), statementExecutions);
    EXPECT_EQ(after.sqliteTransactionsStarted() - before.sqliteTransactionsStarted(), 1u);
    EXPECT_EQ(after.sqliteTransactionsCommitted() - before.sqliteTransactionsCommitted(), committedTransactions);
    EXPECT_EQ(after.sqliteTransactionsRolledBack() - before.sqliteTransactionsRolledBack(), rolledBackTransactions);
}
} // namespace

TEST(CometGuidedAutoTracker, ImageIntegrationFitModelMapsToEngineFitPolicy)
{
    RegistrationSettings::CometFitOptions settings;

    settings.model = QStringLiteral("linear");
    EXPECT_EQ(an::ui::makeCometGuidedAutoTrackingFitOptions(settings).modelPolicy,
              an::CometTrackModelPolicy::ForceLinear);

    settings.model = QStringLiteral("quadratic");
    EXPECT_EQ(an::ui::makeCometGuidedAutoTrackingFitOptions(settings).modelPolicy,
              an::CometTrackModelPolicy::PreferQuadratic);

    settings.model = QStringLiteral("quadratic-preferred");
    EXPECT_EQ(an::ui::makeCometGuidedAutoTrackingFitOptions(settings).modelPolicy,
              an::CometTrackModelPolicy::PreferQuadratic);

    settings.model = QStringLiteral("automatic");
    EXPECT_EQ(an::ui::makeCometGuidedAutoTrackingFitOptions(settings).modelPolicy,
              an::CometTrackModelPolicy::Automatic);

    settings.model = QStringLiteral("auto");
    EXPECT_EQ(an::ui::makeCometGuidedAutoTrackingFitOptions(settings).modelPolicy,
              an::CometTrackModelPolicy::Automatic);
}

TEST(CometGuidedAutoTracker, DefaultLinearFitPreventsAutomaticQuadraticSelection)
{
    an::ui::CometGuidedAutoTrackingRequest request;
    for (int index = 0; index < 6; ++index)
    {
        const QString frame = QStringLiteral("C:/lights/default_linear_%1.fit").arg(index);
        const double epoch = static_cast<double>(index);
        request.frames.push_back(makeGuidedTrackingFrame(frame, epoch));
        request.observations.push_back(RegistrationSettings::CometTrackingObservation{
            FrameId(frame).canonicalPath(), QPointF(10.0 + epoch, 20.0 + epoch * epoch),
            RegistrationSettings::CometObservationOrigin::Manual, true});
    }

    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession(1u);
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    an::Result<an::consumer::OperationScope> scope = admitTrackingOperation(*session);
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();

    const auto result = an::ui::runCometGuidedAutoTracking(scope->execution(), request);

    ASSERT_TRUE(result.succeeded) << result.message.toStdString();
    ASSERT_TRUE(result.productionFitAvailable);
    ASSERT_TRUE(result.fit.has_value());
    EXPECT_EQ(result.fit->model, an::CometTrackModel::Linear);

    auto automatic_request = request;
    automatic_request.fitOptions.model = QStringLiteral("automatic");
    const auto automatic_result = an::ui::runCometGuidedAutoTracking(scope->execution(), automatic_request);

    ASSERT_TRUE(automatic_result.succeeded) << automatic_result.message.toStdString();
    ASSERT_TRUE(automatic_result.fit.has_value());
    EXPECT_EQ(automatic_result.fit->model, an::CometTrackModel::Quadratic);
}

TEST(CometRegistrationProductBridge, AutomaticCandidatePointRecordPreservesRefinementMetrics)
{
    const QString frame_a = QStringLiteral("C:/lights/metrics_a.fit");
    const QString frame_b = QStringLiteral("C:/lights/metrics_b.fit");
    const QString frame_c = QStringLiteral("C:/lights/metrics_c.fit");

    an::ui::CometGuidedAutoTrackingRequest request;
    request.frames = {makeGuidedTrackingFrame(frame_a, 0.0), makeGuidedTrackingFrame(frame_b, 10.0),
                      makeGuidedTrackingFrame(frame_c, 20.0)};
    request.observations = {
        RegistrationSettings::CometTrackingObservation{FrameId(frame_a).canonicalPath(), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{FrameId(frame_c).canonicalPath(), QPointF(30.0, 18.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    request.trackingOptions.searchRadiusPixels = 8.0;
    request.trackingOptions.minimumSnr = 3.0;

    // The track predicts the middle frame's nucleus at (20, 14); the frame holds it in the pixel whose centre is
    // (20.5, 14.5).
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession(1u);
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    const an::Result<an::Image<float>> image = makeGuidedTrackingImage(*session, an::PixelCoordinate{20.5, 14.5});
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    an::Result<an::consumer::OperationScope> scope = admitTrackingOperation(*session);
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    request.refiner = imageRefiner(scope->execution(), *image, FrameId(frame_b));

    const auto result = an::ui::runCometGuidedAutoTracking(scope->execution(), request);
    ASSERT_TRUE(result.succeeded) << result.message.toStdString();
    ASSERT_EQ(result.candidates.size(), 1U);

    const auto* diagnostic = findGuidedTrackingDiagnostic(result, frame_b);
    ASSERT_NE(diagnostic, nullptr);
    ASSERT_EQ(diagnostic->status, an::ui::CometGuidedAutoTrackingStatus::Candidate);

    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = request.observations;
    const an::ui::CometTrackingPointRecord point =
        an::ui::cometRegistrationBridgePointRecord(QStringLiteral("run-metrics"), settings, *diagnostic, result, 42);

    EXPECT_EQ(point.measurementMethod, QStringLiteral("auto_refine"));
    EXPECT_GT(point.measuredFwhmPixels, 0.0);
    EXPECT_GT(point.measuredDiameterPixels, 0.0);
    EXPECT_GE(point.measuredEllipticity, 0.0);
    EXPECT_GT(point.apertureRadiusPixels, 0.0);
    EXPECT_GT(point.localBackground, 0.0);
    EXPECT_GT(point.snr, 0.0);
    EXPECT_GT(point.roiWidth, 0.0);
    EXPECT_GT(point.roiHeight, 0.0);
    EXPECT_DOUBLE_EQ(point.measuredDiameterPixels, point.apertureRadiusPixels * 2.0);
}

TEST(CometRegistrationProductBridge, ManualObservationApertureDoesNotInventMeasuredValues)
{
    const QString frame = QStringLiteral("C:/lights/manual_aperture.fit");

    RegistrationSettings::CometTrackingObservation observation;
    observation.frameId = FrameId(frame).canonicalPath();
    observation.sourcePosition = QPointF(12.0, 14.0);
    observation.origin = RegistrationSettings::CometObservationOrigin::Manual;
    observation.apertureRadiusPixels = 6.5;
    observation.enabled = true;

    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {observation};

    an::ui::CometGuidedAutoTrackingDiagnostic diagnostic;
    diagnostic.originalFrameId = FrameId(frame);
    diagnostic.refinedSourcePosition = an::PixelCoordinate{12.0, 14.0};
    diagnostic.refinedReferencePlanePosition = an::PixelCoordinate{12.0, 14.0};
    diagnostic.status = an::ui::CometGuidedAutoTrackingStatus::AcceptedObservation;

    an::ui::CometGuidedAutoTrackingResult tracking;
    tracking.succeeded = true;
    tracking.diagnostics = {diagnostic};

    const an::ui::CometTrackingPointRecord point =
        an::ui::cometRegistrationBridgePointRecord(QStringLiteral("run-manual"), settings, diagnostic, tracking, 42);

    EXPECT_EQ(point.measurementMethod, QStringLiteral("manual"));
    EXPECT_DOUBLE_EQ(point.apertureRadiusPixels, observation.apertureRadiusPixels);
    EXPECT_DOUBLE_EQ(point.measuredFwhmPixels, 0.0);
    EXPECT_DOUBLE_EQ(point.measuredDiameterPixels, 0.0);
    EXPECT_DOUBLE_EQ(point.measuredEllipticity, 0.0);
    EXPECT_DOUBLE_EQ(point.localBackground, 0.0);
    EXPECT_DOUBLE_EQ(point.snr, 0.0);
    EXPECT_DOUBLE_EQ(point.roiWidth, 0.0);
    EXPECT_DOUBLE_EQ(point.roiHeight, 0.0);
}

TEST_F(TempDirFixture, SqliteConnectionCountsEveryStatementAttemptAndRetryExactly)
{
    WorkflowExecutionCounterAccumulator counters;
    core::SqliteConnection connection(QStringLiteral(":memory:"), QStringLiteral("CounterSqliteConnection"), &counters);
    QString error;
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    QSqlQuery query(connection.db());
    EXPECT_TRUE(connection.execute(query, QStringLiteral("CREATE TABLE counter_rows (value INTEGER NOT NULL)")));
    EXPECT_FALSE(connection.execute(query, QStringLiteral("INSERT INTO missing_counter_table VALUES (1)")));
    EXPECT_FALSE(connection.execute(query, QStringLiteral("INSERT INTO missing_counter_table VALUES (1)")));

    EXPECT_EQ(counters.snapshot().sqliteStatementExecutions(), 3u);
    EXPECT_EQ(counters.snapshot().sqliteTransactionsStarted(), 0u);
    EXPECT_EQ(counters.snapshot().sqliteTransactionsCommitted(), 0u);
    EXPECT_EQ(counters.snapshot().sqliteTransactionsRolledBack(), 0u);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterCountsSuccessfulTransactionAndNoOpExactly)
{
    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(metricsDbPath(tempPath()), QStringLiteral("CounterCommitWriter"),
                                             &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const WorkflowExecutionCounters before = counters.snapshot();
    ASSERT_TRUE(writer.copyMetricsToSettingsVersion(QStringLiteral("counter-group"), 1, 2, &error))
        << error.toStdString();
    const WorkflowExecutionCounters afterCommit = counters.snapshot();

    EXPECT_EQ(afterCommit.sqliteStatementExecutions() - before.sqliteStatementExecutions(), 2u);
    EXPECT_EQ(afterCommit.sqliteTransactionsStarted() - before.sqliteTransactionsStarted(), 1u);
    EXPECT_EQ(afterCommit.sqliteTransactionsCommitted() - before.sqliteTransactionsCommitted(), 1u);
    EXPECT_EQ(afterCommit.sqliteTransactionsRolledBack() - before.sqliteTransactionsRolledBack(), 0u);

    ASSERT_TRUE(writer.copyMetricsToSettingsVersion(QStringLiteral("counter-group"), 2, 2, &error))
        << error.toStdString();
    EXPECT_EQ(counters.snapshot().sqliteStatementExecutions(), afterCommit.sqliteStatementExecutions());
    EXPECT_EQ(counters.snapshot().sqliteTransactionsStarted(), afterCommit.sqliteTransactionsStarted());
    EXPECT_EQ(counters.snapshot().sqliteTransactionsCommitted(), afterCommit.sqliteTransactionsCommitted());
    EXPECT_EQ(counters.snapshot().sqliteTransactionsRolledBack(), afterCommit.sqliteTransactionsRolledBack());
}

TEST_F(TempDirFixture, RegistrationMetricsWriterFailedTransactionCountsRollbackWithoutCommit)
{
    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(metricsDbPath(tempPath()), QStringLiteral("CounterRollbackWriter"),
                                             &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const WorkflowExecutionCounters before = counters.snapshot();
    EXPECT_FALSE(writer.activateCometTrackingRun(QStringLiteral("missing-run"), {QStringLiteral("comet_aligned")},
                                                 false, &error));
    EXPECT_FALSE(error.isEmpty());
    const WorkflowExecutionCounters after = counters.snapshot();

    EXPECT_EQ(after.sqliteStatementExecutions() - before.sqliteStatementExecutions(), 1u);
    EXPECT_EQ(after.sqliteTransactionsStarted() - before.sqliteTransactionsStarted(), 1u);
    EXPECT_EQ(after.sqliteTransactionsCommitted() - before.sqliteTransactionsCommitted(), 0u);
    EXPECT_EQ(after.sqliteTransactionsRolledBack() - before.sqliteTransactionsRolledBack(), 1u);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterMetricsBatchPersistsOneThousandWithLinearCounters)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|metrics-1000");
    constexpr int kSettingsVersion = 17;
    constexpr int kBatchSize = 1000;
    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupMetricsBatchWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    std::vector<an::ui::RegistrationMetricRecord> metrics;
    metrics.reserve(kBatchSize);
    for (int index = 0; index < kBatchSize; ++index)
    {
        const FrameId frame(
            QDir(tempPath()).filePath(QStringLiteral("metrics/light_%1.fit").arg(index, 4, 10, QChar('0'))));
        auto metric =
            makeReferenceMetric(frame, groupKey, kSettingsVersion, 100 + index, 1.5 + index * 0.001, 20.0 + index);
        metric.metrics.eccentricity = 0.2 + index * 0.0001;
        metric.updatedAt = 1700100000 + index;
        metrics.push_back(std::move(metric));
    }

    const WorkflowExecutionCounters before = counters.snapshot();
    ASSERT_TRUE(writer.upsertMetricsForGroup(groupKey, kSettingsVersion, metrics, &error)) << error.toStdString();
    const WorkflowExecutionCounters after = counters.snapshot();
    // One identity-cleanup scan plus one prepared upsert execution per metric.
    expectPersistenceCounterDelta(before, after, kBatchSize + 1, 1u, 0u);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("GroupMetricsBatchLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMetricRecord> loaded;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), static_cast<size_t>(kBatchSize));

    const auto* first = findMetricRecord(loaded, metrics.front().frameId.canonicalPath());
    const auto* last = findMetricRecord(loaded, metrics.back().frameId.canonicalPath());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(first->metrics.stars, metrics.front().metrics.stars);
    EXPECT_DOUBLE_EQ(first->metrics.fwhm, metrics.front().metrics.fwhm);
    EXPECT_EQ(first->outputPath, metrics.front().outputPath);
    EXPECT_EQ(last->metrics.stars, metrics.back().metrics.stars);
    EXPECT_DOUBLE_EQ(last->metrics.snr, metrics.back().metrics.snr);
    EXPECT_EQ(last->transformJson, metrics.back().transformJson);
}

TEST_F(TempDirFixture, RegistrationMetricReadsAndBatchCleanupUseFrameIdentityAndNewestPayload)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|ident|metric-identity");
    constexpr int kSettingsVersion = 121;
    const FrameId currentFrame(QStringLiteral("C:/Ident/Metrics/Light.fit"));
    const FrameId aliasFrame(QStringLiteral("C:/ident/metrics/light.fit"));
    const QString aliasStoredFrame = QStringLiteral("c:\\ident\\metrics\\light.fit");

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("IdentMetricIdentityWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    auto current = makeReferenceMetric(currentFrame, groupKey, kSettingsVersion, 10, 3.0, 5.0);
    current.updatedAt = 100;
    ASSERT_TRUE(writer.upsertMetric(current, &error)) << error.toStdString();

    auto alias = makeReferenceMetric(aliasFrame, groupKey, kSettingsVersion, 20, 2.0, 10.0);
    alias.updatedAt = 200;
    ASSERT_TRUE(writer.upsertMetric(alias, &error)) << error.toStdString();
    replaceStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, aliasFrame.canonicalPath(), aliasStoredFrame);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("IdentMetricIdentityLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMetricRecord> loaded;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.front().metrics.stars, alias.metrics.stars);
    EXPECT_EQ(loaded.front().updatedAt, alias.updatedAt);

    auto replacement = makeReferenceMetric(currentFrame, groupKey, kSettingsVersion, 30, 1.5, 20.0);
    replacement.updatedAt = 300;
    ASSERT_TRUE(writer.upsertMetricsForGroup(groupKey, kSettingsVersion, {replacement}, &error)) << error.toStdString();

    core::SqliteConnection reader(dbPath, QStringLiteral("IdentMetricIdentityReader"));
    ASSERT_TRUE(reader.open(&error)) << error.toStdString();
    QSqlQuery query(reader.db());
    query.prepare(QStringLiteral("SELECT frame_id, stars FROM registration_metrics "
                                 "WHERE group_key = :group_key AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), kSettingsVersion);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toString(), currentFrame.canonicalPath());
    EXPECT_EQ(query.value(1).toInt(), replacement.metrics.stars);
    EXPECT_FALSE(query.next());
}

TEST_F(TempDirFixture, RegistrationMetricEqualTimestampAliasesUseStableStoredSpellingTieBreak)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|ident|metric-equal-timestamp-aliases");
    constexpr int kSettingsVersion = 122;
    const FrameId frame(QStringLiteral("C:/Ident/Tie/Light.fit"));
    const QString forwardSlashAlias = QStringLiteral("c:/ident/tie/light.fit");
    const QString backslashAlias = QStringLiteral("c:\\ident\\tie\\light.fit");
    ASSERT_EQ(FrameId(forwardSlashAlias), frame);
    ASSERT_EQ(FrameId(backslashAlias), frame);
    ASSERT_LT(QString::compare(forwardSlashAlias, backslashAlias, Qt::CaseSensitive), 0);

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("IdentMetricTieWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    auto metric = makeReferenceMetric(frame, groupKey, kSettingsVersion, 10, 3.0, 5.0);
    metric.outputPath = QStringLiteral("registered/backslash-selected-if-order-dependent.fit");
    metric.updatedAt = 500;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    replaceStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, frame.canonicalPath(), backslashAlias);
    const QString expectedOutput = QStringLiteral("registered/forward-slash-stable-winner.fit");
    duplicateStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, backslashAlias, forwardSlashAlias, expectedOutput);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("IdentMetricTieLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMetricRecord> loaded;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.front().frameId, frame);
    EXPECT_EQ(loaded.front().outputPath, expectedOutput);
    EXPECT_EQ(loaded.front().updatedAt, metric.updatedAt);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterStarCacheBatchUsesOneTransactionAndLinearStatements)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|star-cache");
    const QString detectionHash = QStringLiteral("det-batch-batch");
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("stars/a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("stars/b.fit")));
    const FrameId frameC(QDir(tempPath()).filePath(QStringLiteral("stars/c.fit")));
    std::vector<an::ui::RegistrationStarCacheEntry> entries = {
        makeBatchStarCacheEntry(frameA, groupKey, detectionHash, 0),
        makeBatchStarCacheEntry(frameB, groupKey, detectionHash, 1),
        makeBatchStarCacheEntry(frameC, groupKey, detectionHash, 2)};

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupStarBatchWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const WorkflowExecutionCounters before = counters.snapshot();
    ASSERT_TRUE(writer.upsertStarCacheForGroup(groupKey, detectionHash, entries, &error)) << error.toStdString();
    const WorkflowExecutionCounters after = counters.snapshot();
    // One identity-cleanup scan plus one prepared upsert execution per entry.
    expectPersistenceCounterDelta(before, after, entries.size() + 1u, 1u, 0u);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("GroupStarBatchLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::unordered_map<QString, an::ui::RegistrationStarCacheEntry> loaded;
    ASSERT_TRUE(loader.loadStarCacheForGroup(groupKey, detectionHash, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), entries.size());
    const auto loadedB = loaded.find(frameB.canonicalPath());
    ASSERT_NE(loadedB, loaded.end());
    EXPECT_EQ(loadedB->second.inputPath, entries[1].inputPath);
    EXPECT_EQ(loadedB->second.inputMtime, entries[1].inputMtime);
    EXPECT_EQ(loadedB->second.starsCount, entries[1].starsCount);
    EXPECT_DOUBLE_EQ(loadedB->second.fwhm, entries[1].fwhm);
    EXPECT_EQ(loadedB->second.starsBlob, entries[1].starsBlob);
}

TEST_F(TempDirFixture, RegistrationStarCacheIdentityCleanupRejectsDuplicatesAndLoadsOnePreferredPayload)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|ident|star-identity");
    const QString detectionHash = QStringLiteral("det-ident-identity");
    const FrameId currentFrame(QStringLiteral("C:/Ident/Stars/Light.fit"));
    const FrameId aliasFrame(QStringLiteral("C:/ident/stars/light.fit"));
    const QString aliasStoredFrame = QStringLiteral("c:\\ident\\stars\\light.fit");

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("IdentStarIdentityWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    auto current = makeBatchStarCacheEntry(currentFrame, groupKey, detectionHash, 1);
    current.updatedAt = 200;
    ASSERT_TRUE(writer.upsertStarCache(current, &error)) << error.toStdString();
    auto alias = makeBatchStarCacheEntry(aliasFrame, groupKey, detectionHash, 9);
    alias.updatedAt = 200;
    ASSERT_TRUE(writer.upsertStarCache(alias, &error)) << error.toStdString();
    replaceStoredStarCacheFrameId(dbPath, groupKey, detectionHash, aliasFrame.canonicalPath(), aliasStoredFrame);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("IdentStarIdentityLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::unordered_map<QString, an::ui::RegistrationStarCacheEntry> loaded;
    ASSERT_TRUE(loader.loadStarCacheForGroup(groupKey, detectionHash, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.begin()->second.frameId.canonicalPath(), currentFrame.canonicalPath());
    EXPECT_EQ(loaded.begin()->second.starsCount, current.starsCount);

    auto replacement = makeBatchStarCacheEntry(currentFrame, groupKey, detectionHash, 20);
    replacement.updatedAt = 300;
    ASSERT_TRUE(writer.upsertStarCacheForGroup(groupKey, detectionHash, {replacement}, &error)) << error.toStdString();

    core::SqliteConnection reader(dbPath, QStringLiteral("IdentStarIdentityReader"));
    ASSERT_TRUE(reader.open(&error)) << error.toStdString();
    QSqlQuery query(reader.db());
    query.prepare(QStringLiteral("SELECT frame_id, stars_count FROM star_detection_cache "
                                 "WHERE group_key = :group_key AND detection_hash = :detection_hash"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":detection_hash"), detectionHash);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toString(), currentFrame.canonicalPath());
    EXPECT_EQ(query.value(1).toInt(), replacement.starsCount);
    EXPECT_FALSE(query.next());

    auto duplicate = replacement;
    duplicate.frameId = aliasFrame;
    EXPECT_FALSE(writer.upsertStarCacheForGroup(groupKey, detectionHash, {replacement, duplicate}, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("duplicate frame identity"), Qt::CaseInsensitive));

    loaded.clear();
    ASSERT_TRUE(loader.loadStarCacheForGroup(groupKey, detectionHash, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.begin()->second.starsCount, replacement.starsCount);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterMatchedSourceReplacementBatchIsAtomicAndExact)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|matched");
    constexpr int kSettingsVersion = 19;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("matched/a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("matched/b.fit")));
    const FrameId frameC(QDir(tempPath()).filePath(QStringLiteral("matched/c.fit")));

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupMatchedBatchWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMatchedSource(matchedSourceRecord(frameA, groupKey, kSettingsVersion,
                                                               makeMatchedSourceReference(589012345678904001ULL),
                                                               kLookupStaleMatch, 1700200000),
                                           &error))
        << error.toStdString();
    ASSERT_TRUE(writer.upsertMatchedSource(matchedSourceRecord(frameB, groupKey, kSettingsVersion,
                                                               makeMatchedSourceReference(589012345678904002ULL),
                                                               kLookupStaleMatch, 1700200001),
                                           &error))
        << error.toStdString();

    const auto replacementA1 = makeMatchedSourceReference(589012345678904011ULL);
    const auto replacementA2 = makeMatchedSourceReference(589012345678904012ULL);
    const auto replacementC = makeMatchedSourceReference(589012345678904013ULL);
    const std::vector<an::ui::RegistrationMatchedSourceReplacement> replacements = {
        {frameA, {replacementA1, replacementA2}, 1700200010},
        {frameB, {}, 1700200011},
        {frameC, {replacementC}, 1700200012}};

    const WorkflowExecutionCounters before = counters.snapshot();
    ASSERT_TRUE(writer.replaceMatchedSourcesForGroup(groupKey, kSettingsVersion, replacements, &error))
        << error.toStdString();
    const WorkflowExecutionCounters after = counters.snapshot();
    expectPersistenceCounterDelta(before, after, 6u, 1u, 0u);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("GroupMatchedBatchLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMatchedSourceRecord> loaded;
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 3u);
    EXPECT_NE(findMatchedSourceRecord(loaded, frameA.canonicalPath(), replacementA1.sourceIdentifier), nullptr);
    EXPECT_NE(findMatchedSourceRecord(loaded, frameA.canonicalPath(), replacementA2.sourceIdentifier), nullptr);
    EXPECT_NE(findMatchedSourceRecord(loaded, frameC.canonicalPath(), replacementC.sourceIdentifier), nullptr);
    EXPECT_EQ(findMatchedSourceRecord(loaded, frameB.canonicalPath(), 589012345678904002ULL), nullptr);

    const std::vector<an::ui::RegistrationMatchedSourceReplacement> duplicate_sources = {
        {frameA, {replacementA1, replacementA1}, 1700200020}};
    EXPECT_FALSE(writer.replaceMatchedSourcesForGroup(groupKey, kSettingsVersion, duplicate_sources, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));

    loaded.clear();
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    EXPECT_EQ(loaded.size(), 3u);
}

TEST_F(TempDirFixture, MatchedSourceReplacementCollapsesAliasesAndRejectsDuplicateFrameIdentities)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|ident|matched-identity");
    constexpr int kSettingsVersion = 121;
    const FrameId currentFrame(QStringLiteral("C:/Ident/Matched/Light.fit"));
    const FrameId aliasFrame(QStringLiteral("C:/ident/matched/light.fit"));
    const QString aliasStoredFrame = QStringLiteral("c:\\ident\\matched\\light.fit");
    const auto currentSource = makeMatchedSourceReference(589012345678921001ULL);
    const auto aliasSource = makeMatchedSourceReference(589012345678921002ULL);
    const auto replacementSource = makeMatchedSourceReference(589012345678921003ULL);

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("IdentMatchedIdentityWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMatchedSource(
        matchedSourceRecord(currentFrame, groupKey, kSettingsVersion, currentSource, kLookupStaleMatch, 100), &error))
        << error.toStdString();
    ASSERT_TRUE(writer.upsertMatchedSource(
        matchedSourceRecord(aliasFrame, groupKey, kSettingsVersion, aliasSource, kLookupSatisfied, 200), &error))
        << error.toStdString();
    replaceStoredMatchedSourceFrameId(dbPath, groupKey, kSettingsVersion, QString::number(aliasSource.sourceIdentifier),
                                      aliasFrame.canonicalPath(), aliasStoredFrame);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("IdentMatchedIdentityLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMatchedSourceRecord> loaded;
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.front().reference.sourceIdentifier, aliasSource.sourceIdentifier);
    ASSERT_TRUE(loader.loadMatchedSourcesForFrame(groupKey, kSettingsVersion, currentFrame, loaded, &error))
        << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.front().reference.sourceIdentifier, aliasSource.sourceIdentifier);

    const std::vector<an::ui::RegistrationMatchedSourceReplacement> replacements = {
        {currentFrame, {replacementSource}, 300}};
    ASSERT_TRUE(writer.replaceMatchedSourcesForGroup(groupKey, kSettingsVersion, replacements, &error))
        << error.toStdString();

    auto duplicateSource = replacementSource;
    duplicateSource.sourceIdentifier = 589012345678921004ULL;
    const std::vector<an::ui::RegistrationMatchedSourceReplacement> duplicates = {
        {currentFrame, {replacementSource}, 400}, {aliasFrame, {duplicateSource}, 400}};
    EXPECT_FALSE(writer.replaceMatchedSourcesForGroup(groupKey, kSettingsVersion, duplicates, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("duplicate frame identity"), Qt::CaseInsensitive));

    loaded.clear();
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.front().frameId.canonicalPath(), currentFrame.canonicalPath());
    EXPECT_EQ(loaded.front().reference.sourceIdentifier, replacementSource.sourceIdentifier);

    const auto lateAliasSource = makeMatchedSourceReference(589012345678921005ULL);
    ASSERT_TRUE(writer.upsertMatchedSource(
        matchedSourceRecord(aliasFrame, groupKey, kSettingsVersion, lateAliasSource, kLookupStaleMatch, 500), &error))
        << error.toStdString();
    replaceStoredMatchedSourceFrameId(dbPath, groupKey, kSettingsVersion,
                                      QString::number(lateAliasSource.sourceIdentifier), aliasFrame.canonicalPath(),
                                      aliasStoredFrame);

    const auto singleFrameReplacement = makeMatchedSourceReference(589012345678921006ULL);
    ASSERT_TRUE(writer.replaceMatchedSourcesForFrame(groupKey, kSettingsVersion, currentFrame, {singleFrameReplacement},
                                                     &error))
        << error.toStdString();
    loaded.clear();
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.front().frameId.canonicalPath(), currentFrame.canonicalPath());
    EXPECT_EQ(loaded.front().reference.sourceIdentifier, singleFrameReplacement.sourceIdentifier);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterAlignedOutputPathBatchUsesOneTransactionAndPreservesMetrics)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|aligned-paths");
    constexpr int kSettingsVersion = 23;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("aligned/a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("aligned/b.fit")));
    auto metricA = makeReferenceMetric(frameA, groupKey, kSettingsVersion, 120, 1.8, 35.0);
    auto metricB = makeReferenceMetric(frameB, groupKey, kSettingsVersion, 90, 2.3, 24.0);
    const QString updatedA = QDir(tempPath()).filePath(QStringLiteral("registered/a_aligned.fit"));
    const QString updatedB = QDir(tempPath()).filePath(QStringLiteral("registered/b_aligned.fit"));

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupAlignedBatchWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(metricA, &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(metricB, &error)) << error.toStdString();

    const std::vector<an::ui::RegistrationAlignedOutputPathUpdate> updates = {
        {frameA, updatedA, 12345, 987654321, QStringLiteral("identity-a")},
        {frameB, updatedB, 54321, 987654322, QStringLiteral("identity-b")}};
    const WorkflowExecutionCounters before = counters.snapshot();
    ASSERT_TRUE(writer.updateAlignedOutputPathsForGroup(groupKey, kSettingsVersion, updates, &error))
        << error.toStdString();
    const WorkflowExecutionCounters after = counters.snapshot();
    expectPersistenceCounterDelta(before, after, updates.size() + 1u, 1u, 0u);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("GroupAlignedBatchLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMetricRecord> loaded;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 2u);
    const auto* loadedA = findMetricRecord(loaded, frameA.canonicalPath());
    const auto* loadedB = findMetricRecord(loaded, frameB.canonicalPath());
    ASSERT_NE(loadedA, nullptr);
    ASSERT_NE(loadedB, nullptr);
    EXPECT_EQ(loadedA->outputPath, updatedA);
    EXPECT_EQ(loadedB->outputPath, updatedB);
    EXPECT_EQ(loadedA->outputSizeBytes, 12345);
    EXPECT_EQ(loadedA->outputModifiedAtMs, 987654321);
    EXPECT_EQ(loadedA->outputIdentityToken, QStringLiteral("identity-a"));
    EXPECT_EQ(loadedB->outputSizeBytes, 54321);
    EXPECT_EQ(loadedB->outputModifiedAtMs, 987654322);
    EXPECT_EQ(loadedB->outputIdentityToken, QStringLiteral("identity-b"));
    EXPECT_EQ(loadedA->metrics.stars, metricA.metrics.stars);
    EXPECT_EQ(loadedB->transformJson, metricB.transformJson);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterAlignedOutputBatchIndexesAliasRowsOnceWithLinearLookups)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|aligned-alias-index");
    constexpr int kSettingsVersion = 29;
    const FrameId frameA(QStringLiteral("C:/Alias/Spelling/A.fit"));
    const FrameId frameB(QStringLiteral("C:/Alias/Spelling/B.fit"));
    const FrameId frameC(QStringLiteral("C:/Alias/Spelling/C.fit"));
    const std::vector<FrameId> frames = {frameA, frameB, frameC};

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("AliasAlignedIndexWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    for (int index = 0; index < static_cast<int>(frames.size()); ++index)
    {
        ASSERT_TRUE(writer.upsertMetric(
            makeReferenceMetric(frames[static_cast<size_t>(index)], groupKey, kSettingsVersion, 100 + index, 2.0, 30.0),
            &error))
            << error.toStdString();
    }

    std::vector<QString> storedFrameIds;
    storedFrameIds.reserve(frames.size());
    for (const FrameId& frame : frames)
    {
        QString alias = frame.canonicalPath().toLower();
        alias.replace(QLatin1Char('/'), QLatin1Char('\\'));
        storedFrameIds.push_back(alias);
        replaceStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, frame.canonicalPath(), alias);
    }

    const std::vector<an::ui::RegistrationAlignedOutputPathUpdate> updates = {
        {frameA, QStringLiteral("registered/alias-a.fit")},
        {frameB, QStringLiteral("registered/alias-b.fit")},
        {frameC, QStringLiteral("registered/alias-c.fit")}};
    an::ui::RegistrationAlignedOutputPathBatchDiagnostics diagnostics;
    const WorkflowExecutionCounters before = counters.snapshot();
    ASSERT_TRUE(writer.updateAlignedOutputPathsForGroup(groupKey, kSettingsVersion, updates, &error, &diagnostics))
        << error.toStdString();
    const WorkflowExecutionCounters after = counters.snapshot();

    expectPersistenceCounterDelta(before, after, updates.size() + 1u, 1u, 0u);
    EXPECT_EQ(diagnostics.groupRowScans, 1U);
    EXPECT_EQ(diagnostics.storedRowsIndexed, frames.size());
    EXPECT_EQ(diagnostics.canonicalLookups, updates.size());
    EXPECT_EQ(diagnostics.resolvedUpdates, updates.size());
    EXPECT_EQ(diagnostics.canonicalCollisions, 0U);
    for (size_t index = 0; index < updates.size(); ++index)
    {
        EXPECT_EQ(storedMetricOutputPath(dbPath, groupKey, kSettingsVersion, storedFrameIds[index]),
                  updates[index].outputPath);
    }
}

TEST_F(TempDirFixture, RegistrationMetricsWriterAlignedOutputBatchUpdatesAllAliasesAndNewestReadback)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|aligned-collision");
    constexpr int kSettingsVersion = 31;
    const FrameId frame(QStringLiteral("C:/Alias/Collision/Light.fit"));
    const QString canonicalFrameId = frame.canonicalPath();
    QString aliasFrameId = canonicalFrameId.toLower();
    aliasFrameId.replace(QLatin1Char('/'), QLatin1Char('\\'));
    const QString canonicalOldOutput = QStringLiteral("registered/canonical-old.fit");
    const QString aliasOldOutput = QStringLiteral("registered/alias-old.fit");
    const QString updatedOutput = QStringLiteral("registered/canonical-updated.fit");

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("AliasAlignedCollisionWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    auto metric = makeReferenceMetric(frame, groupKey, kSettingsVersion, 100, 2.0, 30.0);
    metric.outputPath = canonicalOldOutput;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    duplicateStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, canonicalFrameId, aliasFrameId, aliasOldOutput,
                                 100);

    const std::vector<an::ui::RegistrationAlignedOutputPathUpdate> updates = {{frame, updatedOutput}};
    an::ui::RegistrationAlignedOutputPathBatchDiagnostics diagnostics;
    const WorkflowExecutionCounters before = counters.snapshot();
    ASSERT_TRUE(writer.updateAlignedOutputPathsForGroup(groupKey, kSettingsVersion, updates, &error, &diagnostics))
        << error.toStdString();
    const WorkflowExecutionCounters after = counters.snapshot();

    expectPersistenceCounterDelta(before, after, 3u, 1u, 0u);
    EXPECT_EQ(diagnostics.groupRowScans, 1U);
    EXPECT_EQ(diagnostics.storedRowsIndexed, 2U);
    EXPECT_EQ(diagnostics.canonicalLookups, 1U);
    EXPECT_EQ(diagnostics.resolvedUpdates, 1U);
    EXPECT_EQ(diagnostics.canonicalCollisions, 1U);
    EXPECT_EQ(storedMetricOutputPath(dbPath, groupKey, kSettingsVersion, canonicalFrameId), updatedOutput);
    EXPECT_EQ(storedMetricOutputPath(dbPath, groupKey, kSettingsVersion, aliasFrameId), updatedOutput);

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("AliasAlignedCollisionLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMetricRecord> loaded;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.front().frameId, frame);
    EXPECT_EQ(loaded.front().outputPath, updatedOutput);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterAlignedOutputBatchUpdatesAllAliases)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|aligned-ambiguous-collision");
    constexpr int kSettingsVersion = 33;
    const FrameId frame(QStringLiteral("C:/Alias/Ambiguous/Light.fit"));
    const QString canonicalFrameId = frame.canonicalPath();
    const QString aliasFrameIdA = QStringLiteral("c:\\alias\\ambiguous\\light.fit");
    const QString aliasFrameIdB = QStringLiteral("C:\\Alias\\AMBIGUOUS\\LIGHT.FIT");
    const QString aliasOutputA = QStringLiteral("registered/alias-a-old.fit");
    const QString aliasOutputB = QStringLiteral("registered/alias-b-old.fit");

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("AliasAlignedAmbiguousWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    auto metric = makeReferenceMetric(frame, groupKey, kSettingsVersion, 100, 2.0, 30.0);
    metric.outputPath = aliasOutputA;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    replaceStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, canonicalFrameId, aliasFrameIdA);
    duplicateStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, aliasFrameIdA, aliasFrameIdB, aliasOutputB);

    const QString updatedOutput = QStringLiteral("registered/all-aliases-updated.fit");
    const std::vector<an::ui::RegistrationAlignedOutputPathUpdate> updates = {{frame, updatedOutput}};
    an::ui::RegistrationAlignedOutputPathBatchDiagnostics diagnostics;
    const WorkflowExecutionCounters before = counters.snapshot();
    ASSERT_TRUE(writer.updateAlignedOutputPathsForGroup(groupKey, kSettingsVersion, updates, &error, &diagnostics))
        << error.toStdString();
    const WorkflowExecutionCounters after = counters.snapshot();

    expectPersistenceCounterDelta(before, after, 3u, 1u, 0u);
    EXPECT_EQ(diagnostics.groupRowScans, 1U);
    EXPECT_EQ(diagnostics.storedRowsIndexed, 2U);
    EXPECT_EQ(diagnostics.canonicalLookups, 1U);
    EXPECT_EQ(diagnostics.resolvedUpdates, 1U);
    EXPECT_EQ(diagnostics.canonicalCollisions, 1U);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(storedMetricOutputPath(dbPath, groupKey, kSettingsVersion, aliasFrameIdA), updatedOutput);
    EXPECT_EQ(storedMetricOutputPath(dbPath, groupKey, kSettingsVersion, aliasFrameIdB), updatedOutput);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterAlignedOutputBatchRejectsDuplicateLogicalUpdates)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|aligned-duplicate-input");
    constexpr int kSettingsVersion = 35;
    const FrameId frame(QStringLiteral("C:/Alias/Duplicate/Light.fit"));

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("AliasAlignedDuplicateWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(makeReferenceMetric(frame, groupKey, kSettingsVersion, 100, 2.0, 30.0), &error))
        << error.toStdString();

    QString aliasFrameId = frame.canonicalPath().toLower();
    aliasFrameId.replace(QLatin1Char('/'), QLatin1Char('\\'));
    const std::vector<an::ui::RegistrationAlignedOutputPathUpdate> updates = {
        {frame, QStringLiteral("registered/first.fit")},
        {FrameId(aliasFrameId), QStringLiteral("registered/second.fit")}};
    const WorkflowExecutionCounters before = counters.snapshot();
    EXPECT_FALSE(writer.updateAlignedOutputPathsForGroup(groupKey, kSettingsVersion, updates, &error));
    const WorkflowExecutionCounters after = counters.snapshot();

    expectPersistenceCounterDelta(before, after, 0u, 0u, 1u);
    EXPECT_TRUE(error.contains(QStringLiteral("batch_item_index=1")));
    EXPECT_TRUE(error.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));
}

TEST_F(TempDirFixture, RegistrationMetricsWriterEmptyBatchesAreSuccessfulZeroWorkNoOps)
{
    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(metricsDbPath(tempPath()), QStringLiteral("GroupEmptyBatchWriter"),
                                             &counters);
    QString error = QStringLiteral("stale");

    EXPECT_TRUE(writer.upsertMetricsForGroup(QStringLiteral("reg|empty"), 1, {}, &error));
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(writer.upsertStarCacheForGroup(QStringLiteral("reg|empty"), QStringLiteral("det-empty"), {}, &error));
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(writer.replaceMatchedSourcesForGroup(QStringLiteral("reg|empty"), 1, {}, &error));
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(writer.updateAlignedOutputPathsForGroup(QStringLiteral("reg|empty"), 1, {}, &error));
    EXPECT_TRUE(error.isEmpty());

    const WorkflowExecutionCounters after = counters.snapshot();
    EXPECT_EQ(after.sqliteStatementExecutions(), 0u);
    EXPECT_EQ(after.sqliteTransactionsStarted(), 0u);
    EXPECT_EQ(after.sqliteTransactionsCommitted(), 0u);
    EXPECT_EQ(after.sqliteTransactionsRolledBack(), 0u);
    EXPECT_FALSE(writer.isOpen());
    EXPECT_FALSE(QFileInfo::exists(metricsDbPath(tempPath())));
}

TEST_F(TempDirFixture, RegistrationMetricsWriterBatchContractsRejectHeterogeneousRecordsBeforeWriting)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|homogeneous");
    constexpr int kSettingsVersion = 29;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("homogeneous/a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("homogeneous/b.fit")));
    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupHomogeneousBatchWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    auto metricA = makeReferenceMetric(frameA, groupKey, kSettingsVersion, 100, 2.0, 30.0);
    auto metricB = makeReferenceMetric(frameB, groupKey, kSettingsVersion + 1, 110, 1.9, 32.0);
    const WorkflowExecutionCounters before = counters.snapshot();
    EXPECT_FALSE(writer.upsertMetricsForGroup(groupKey, kSettingsVersion, {metricA, metricB}, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("metric batch")));
    EXPECT_TRUE(error.contains(QStringLiteral("batch_item_index=1")));
    EXPECT_TRUE(error.contains(frameB.canonicalPath()));
    EXPECT_TRUE(error.contains(groupKey));
    EXPECT_TRUE(error.contains(QStringLiteral("settings_version=30")));

    const auto starA = makeBatchStarCacheEntry(frameA, groupKey, QStringLiteral("det-homogeneous"), 0);
    const auto starB = makeBatchStarCacheEntry(frameB, groupKey, QStringLiteral("det-other"), 1);
    EXPECT_FALSE(writer.upsertStarCacheForGroup(groupKey, QStringLiteral("det-homogeneous"), {starA, starB}, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("star-cache batch")));
    EXPECT_TRUE(error.contains(QStringLiteral("batch_item_index=1")));
    EXPECT_TRUE(error.contains(frameB.canonicalPath()));
    EXPECT_TRUE(error.contains(QStringLiteral("det-other")));

    const WorkflowExecutionCounters after = counters.snapshot();
    EXPECT_EQ(after.sqliteStatementExecutions(), before.sqliteStatementExecutions());
    EXPECT_EQ(after.sqliteTransactionsStarted() - before.sqliteTransactionsStarted(), 2u);
    EXPECT_EQ(after.sqliteTransactionsCommitted(), before.sqliteTransactionsCommitted());
    EXPECT_EQ(after.sqliteTransactionsRolledBack() - before.sqliteTransactionsRolledBack(), 2u);

    core::SqliteConnection reader(dbPath, QStringLiteral("GroupHomogeneousBatchReader"));
    ASSERT_TRUE(reader.open(&error)) << error.toStdString();
    EXPECT_EQ(tableRowCount(reader.db(), QStringLiteral("registration_metrics")), 0);
    EXPECT_EQ(tableRowCount(reader.db(), QStringLiteral("star_detection_cache")), 0);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterMetricsBatchRollsBackWholeGroupWithFailingFrameAttribution)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|metric-rollback");
    constexpr int kSettingsVersion = 31;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("metric_rollback/a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("metric_rollback/b.fit")));
    const QString triggerMessage = QStringLiteral("forced metric failure");
    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupMetricRollbackWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    installFrameAbortTrigger(dbPath, QStringLiteral("batch_abort_metric"), QStringLiteral("INSERT"),
                             QStringLiteral("registration_metrics"), frameB.canonicalPath(), triggerMessage);

    const std::vector<an::ui::RegistrationMetricRecord> metrics = {
        makeReferenceMetric(frameA, groupKey, kSettingsVersion, 100, 2.0, 30.0),
        makeReferenceMetric(frameB, groupKey, kSettingsVersion, 110, 1.9, 32.0)};
    const WorkflowExecutionCounters before = counters.snapshot();
    EXPECT_FALSE(writer.upsertMetricsForGroup(groupKey, kSettingsVersion, metrics, &error));
    const WorkflowExecutionCounters after = counters.snapshot();
    expectPersistenceCounterDelta(before, after, 2u, 0u, 1u);
    EXPECT_TRUE(error.contains(QStringLiteral("metric upsert")));
    EXPECT_TRUE(error.contains(QStringLiteral("batch_item_index=1")));
    EXPECT_TRUE(error.contains(frameB.canonicalPath()));
    EXPECT_TRUE(error.contains(groupKey));
    EXPECT_TRUE(error.contains(QStringLiteral("settings_version=31")));
    EXPECT_TRUE(error.contains(triggerMessage));

    core::SqliteConnection reader(dbPath, QStringLiteral("GroupMetricRollbackReader"));
    ASSERT_TRUE(reader.open(&error)) << error.toStdString();
    EXPECT_EQ(tableRowCount(reader.db(), QStringLiteral("registration_metrics")), 0);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterStarCacheBatchRollsBackWholeGroupWithFailingFrameAttribution)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|star-rollback");
    const QString detectionHash = QStringLiteral("det-batch-rollback");
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("star_rollback/a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("star_rollback/b.fit")));
    const QString triggerMessage = QStringLiteral("forced star-cache failure");
    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupStarRollbackWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    installFrameAbortTrigger(dbPath, QStringLiteral("batch_abort_star"), QStringLiteral("INSERT"),
                             QStringLiteral("star_detection_cache"), frameB.canonicalPath(), triggerMessage);

    const std::vector<an::ui::RegistrationStarCacheEntry> entries = {
        makeBatchStarCacheEntry(frameA, groupKey, detectionHash, 0),
        makeBatchStarCacheEntry(frameB, groupKey, detectionHash, 1)};
    const WorkflowExecutionCounters before = counters.snapshot();
    EXPECT_FALSE(writer.upsertStarCacheForGroup(groupKey, detectionHash, entries, &error));
    const WorkflowExecutionCounters after = counters.snapshot();
    expectPersistenceCounterDelta(before, after, 2u, 0u, 1u);
    EXPECT_TRUE(error.contains(QStringLiteral("star-cache upsert")));
    EXPECT_TRUE(error.contains(QStringLiteral("batch_item_index=1")));
    EXPECT_TRUE(error.contains(frameB.canonicalPath()));
    EXPECT_TRUE(error.contains(groupKey));
    EXPECT_TRUE(error.contains(detectionHash));
    EXPECT_TRUE(error.contains(triggerMessage));

    core::SqliteConnection reader(dbPath, QStringLiteral("GroupStarRollbackReader"));
    ASSERT_TRUE(reader.open(&error)) << error.toStdString();
    EXPECT_EQ(tableRowCount(reader.db(), QStringLiteral("star_detection_cache")), 0);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterMatchedSourceBatchRollsBackEarlierReplacementWithAttribution)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|matched-rollback");
    constexpr int kSettingsVersion = 37;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("matched_rollback/a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("matched_rollback/b.fit")));
    const auto oldA = makeMatchedSourceReference(589012345678905001ULL);
    const auto oldB = makeMatchedSourceReference(589012345678905002ULL);
    const auto replacementA = makeMatchedSourceReference(589012345678905011ULL);
    const auto replacementB = makeMatchedSourceReference(589012345678905012ULL);
    const QString triggerMessage = QStringLiteral("forced matched-source failure");

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupMatchedRollbackWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMatchedSource(
        matchedSourceRecord(frameA, groupKey, kSettingsVersion, oldA, kLookupStaleMatch, 1700300000), &error))
        << error.toStdString();
    ASSERT_TRUE(writer.upsertMatchedSource(
        matchedSourceRecord(frameB, groupKey, kSettingsVersion, oldB, kLookupStaleMatch, 1700300001), &error))
        << error.toStdString();
    installFrameAbortTrigger(dbPath, QStringLiteral("batch_abort_matched"), QStringLiteral("INSERT"),
                             QStringLiteral("registration_matched_sources"), frameB.canonicalPath(), triggerMessage);

    const std::vector<an::ui::RegistrationMatchedSourceReplacement> replacements = {
        {frameA, {replacementA}, 1700300010}, {frameB, {replacementB}, 1700300011}};
    const WorkflowExecutionCounters before = counters.snapshot();
    EXPECT_FALSE(writer.replaceMatchedSourcesForGroup(groupKey, kSettingsVersion, replacements, &error));
    const WorkflowExecutionCounters after = counters.snapshot();
    // One bounded identity scan, two deletes, and two attempted inserts.
    expectPersistenceCounterDelta(before, after, 5u, 0u, 1u);
    EXPECT_TRUE(error.contains(QStringLiteral("matched-source insert")));
    EXPECT_TRUE(error.contains(QStringLiteral("batch_item_index=1")));
    EXPECT_TRUE(error.contains(frameB.canonicalPath()));
    EXPECT_TRUE(error.contains(groupKey));
    EXPECT_TRUE(error.contains(QStringLiteral("settings_version=37")));
    EXPECT_TRUE(error.contains(triggerMessage));

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("GroupMatchedRollbackLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMatchedSourceRecord> loaded;
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_NE(findMatchedSourceRecord(loaded, frameA.canonicalPath(), oldA.sourceIdentifier), nullptr);
    EXPECT_NE(findMatchedSourceRecord(loaded, frameB.canonicalPath(), oldB.sourceIdentifier), nullptr);
    EXPECT_EQ(findMatchedSourceRecord(loaded, frameA.canonicalPath(), replacementA.sourceIdentifier), nullptr);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterAlignedOutputBatchRollsBackEarlierUpdateWithAttribution)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|batch|aligned-rollback");
    constexpr int kSettingsVersion = 41;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("aligned_rollback/a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("aligned_rollback/b.fit")));
    auto metricA = makeReferenceMetric(frameA, groupKey, kSettingsVersion, 100, 2.0, 30.0);
    auto metricB = makeReferenceMetric(frameB, groupKey, kSettingsVersion, 110, 1.9, 32.0);
    const QString oldA = metricA.outputPath;
    const QString oldB = metricB.outputPath;
    const QString triggerMessage = QStringLiteral("forced aligned-output failure");

    WorkflowExecutionCounterAccumulator counters;
    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("GroupAlignedRollbackWriter"), &counters);
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(metricA, &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(metricB, &error)) << error.toStdString();
    installFrameAbortTrigger(dbPath, QStringLiteral("batch_abort_aligned"), QStringLiteral("UPDATE"),
                             QStringLiteral("registration_metrics"), frameB.canonicalPath(), triggerMessage);

    const std::vector<an::ui::RegistrationAlignedOutputPathUpdate> updates = {
        {frameA, QStringLiteral("updated/a.fit")}, {frameB, QStringLiteral("updated/b.fit")}};
    const WorkflowExecutionCounters before = counters.snapshot();
    EXPECT_FALSE(writer.updateAlignedOutputPathsForGroup(groupKey, kSettingsVersion, updates, &error));
    const WorkflowExecutionCounters after = counters.snapshot();
    expectPersistenceCounterDelta(before, after, 3u, 0u, 1u);
    EXPECT_TRUE(error.contains(QStringLiteral("aligned output-path update")));
    EXPECT_TRUE(error.contains(QStringLiteral("batch_item_index=1")));
    EXPECT_TRUE(error.contains(frameB.canonicalPath()));
    EXPECT_TRUE(error.contains(groupKey));
    EXPECT_TRUE(error.contains(QStringLiteral("settings_version=41")));
    EXPECT_TRUE(error.contains(triggerMessage));

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("GroupAlignedRollbackLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMetricRecord> loaded;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.size(), 2u);
    const auto* loadedA = findMetricRecord(loaded, frameA.canonicalPath());
    const auto* loadedB = findMetricRecord(loaded, frameB.canonicalPath());
    ASSERT_NE(loadedA, nullptr);
    ASSERT_NE(loadedB, nullptr);
    EXPECT_EQ(loadedA->outputPath, oldA);
    EXPECT_EQ(loadedB->outputPath, oldB);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterRoundTripsWithRegistrationMetricsRepository)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|writer|roundtrip");
    const int settingsVersion = 7;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("light_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestMetricsWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationGroupState groupState;
    groupState.groupKey = groupKey;
    groupState.settingsVersion = settingsVersion;
    groupState.detectionHash = QStringLiteral("det_hash");
    groupState.registrationHash = QStringLiteral("reg_hash");
    groupState.referenceFrameId = frameA.canonicalPath();
    groupState.lightFilesHash = QStringLiteral("lights_hash");
    groupState.calibrationHash = QStringLiteral("cal_hash");
    groupState.outputWcsJson = QStringLiteral("{\"schemaVersion\":1}");
    ASSERT_TRUE(writer.upsertGroupState(groupState, &error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metricA;
    metricA.frameId = frameA;
    metricA.groupKey = groupKey;
    metricA.settingsVersion = settingsVersion;
    metricA.metrics.stars = 150;
    metricA.metrics.fwhm = 2.1;
    metricA.metrics.snr = 32.0;
    metricA.metrics.eccentricity = 0.08;
    metricA.metrics.photometric_zero_point = 21.7;
    metricA.metrics.zero_point_scatter = 0.12;
    metricA.metrics.astrometric_rms_pixels = 1.4;
    metricA.metrics.astrometric_rms_arcsec = 0.45;
    metricA.metrics.limiting_magnitude = 18.4;
    metricA.metrics.catalog_matched_stars = 44;
    metricA.metrics.succeeded = true;
    metricA.outputPath = QDir(tempPath()).filePath(QStringLiteral("aligned/light_a_aligned.fit"));
    metricA.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(0.0, 0.0));
    ASSERT_TRUE(writer.upsertMetric(metricA, &error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metricB;
    metricB.frameId = frameB;
    metricB.groupKey = groupKey;
    metricB.settingsVersion = settingsVersion;
    metricB.metrics.stars = 92;
    metricB.metrics.fwhm = 2.9;
    metricB.metrics.snr = 19.5;
    metricB.metrics.eccentricity = 0.18;
    metricB.metrics.photometric_zero_point = 20.8;
    metricB.metrics.zero_point_scatter = 0.25;
    metricB.metrics.astrometric_rms_pixels = 2.8;
    metricB.metrics.astrometric_rms_arcsec = 0.9;
    metricB.metrics.limiting_magnitude = 17.6;
    metricB.metrics.catalog_matched_stars = 28;
    metricB.metrics.succeeded = false;
    metricB.outputPath = QStringLiteral("stale_output_should_still_roundtrip");
    metricB.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(-3.0, 1.5));
    ASSERT_TRUE(writer.upsertMetric(metricB, &error)) << error.toStdString();

    RegistrationMetricsRepository repository(dbPath);
    std::vector<RegistrationMetricsCandidate> candidates;
    ASSERT_TRUE(repository.loadCandidates(groupKey, settingsVersion, {frameA, frameB}, candidates, error))
        << error.toStdString();
    ASSERT_EQ(candidates.size(), 2u);

    const auto* candidateA = findCandidate(candidates, frameA.canonicalPath());
    const auto* candidateB = findCandidate(candidates, frameB.canonicalPath());
    ASSERT_NE(candidateA, nullptr);
    ASSERT_NE(candidateB, nullptr);

    EXPECT_EQ(candidateA->metrics.stars, metricA.metrics.stars);
    EXPECT_DOUBLE_EQ(candidateA->metrics.fwhm, metricA.metrics.fwhm);
    EXPECT_DOUBLE_EQ(candidateA->metrics.astrometric_rms_pixels, metricA.metrics.astrometric_rms_pixels);
    EXPECT_DOUBLE_EQ(candidateA->metrics.astrometric_rms_arcsec, metricA.metrics.astrometric_rms_arcsec);
    EXPECT_TRUE(candidateA->metrics.succeeded);

    EXPECT_EQ(candidateB->metrics.stars, metricB.metrics.stars);
    EXPECT_DOUBLE_EQ(candidateB->metrics.snr, metricB.metrics.snr);
    EXPECT_DOUBLE_EQ(candidateB->metrics.astrometric_rms_pixels, metricB.metrics.astrometric_rms_pixels);
    EXPECT_FALSE(candidateB->metrics.succeeded);

    const auto pick = repository.pickBestReference(
        groupKey, settingsVersion, {frameA, frameB}, [](const FrameId&) { return QStringLiteral("L"); }, error);
    ASSERT_TRUE(pick.has_value()) << error.toStdString();
    EXPECT_EQ(pick->id.canonicalPath(), frameA.canonicalPath());
}

TEST_F(TempDirFixture, RegistrationMetricsRepositoryFiltersByFrameIdentityOnWindowsPaths)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|writer|windows-identity");
    constexpr int kSettingsVersion = 9;
    const FrameId storedFrame(QStringLiteral("C:/Images/LIGHT_001.fit"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestWindowsIdentityWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metric;
    metric.frameId = storedFrame;
    metric.groupKey = groupKey;
    metric.settingsVersion = kSettingsVersion;
    metric.metrics.stars = 100;
    metric.metrics.succeeded = true;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();

    RegistrationMetricsRepository repository(dbPath);
    std::vector<RegistrationMetricsCandidate> candidates;
    ASSERT_TRUE(repository.loadCandidates(groupKey, kSettingsVersion,
                                          {FrameId(QStringLiteral("c:\\images\\light_001.fit"))}, candidates, error))
        << error.toStdString();
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates.front().id, storedFrame);
}

TEST_F(TempDirFixture, RegistrationMetricsRepositoryCollapsesAliasesAndKeepsNewestPayload)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|repository|newest-identity");
    constexpr int kSettingsVersion = 10;
    const FrameId frame(QStringLiteral("C:/Alias/Repository/Light.fit"));
    const QString backslashAlias = QStringLiteral("c:\\alias\\repository\\light.fit");
    const QString forwardSlashAlias = QStringLiteral("c:/alias/repository/light.fit");

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("RepositoryNewestIdentityWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    auto metric = makeReferenceMetric(frame, groupKey, kSettingsVersion, 10, 3.0, 5.0);
    metric.updatedAt = 100;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    replaceStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, frame.canonicalPath(), backslashAlias);
    duplicateStoredMetricFrameId(dbPath, groupKey, kSettingsVersion, backslashAlias, forwardSlashAlias,
                                 QStringLiteral("newest.fit"), 100);
    updateStoredMetricStars(dbPath, groupKey, kSettingsVersion, forwardSlashAlias, 99);

    RegistrationMetricsRepository repository(dbPath);
    std::vector<RegistrationMetricsCandidate> candidates;
    ASSERT_TRUE(repository.loadCandidates(groupKey, kSettingsVersion, {frame}, candidates, error))
        << error.toStdString();

    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates.front().id, frame);
    EXPECT_EQ(candidates.front().metrics.stars, 99);
}

TEST_F(TempDirFixture, CometReferenceResolutionExplicitReferenceResolvesDeterministically)
{
    const QString dbPath = metricsDbPath(tempPath());
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("light_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")));

    an::ui::CometReferenceResolutionService service(dbPath);
    an::ui::CometReferenceResolutionRequest request;
    request.groupKey = QStringLiteral("reg|comet|explicit-reference");
    request.settingsVersion = 3;
    request.groupFrames = {frameA, frameB};
    request.autoReference = false;
    request.explicitReferenceFrame = frameB;

    const auto result = service.resolve(request);
    ASSERT_EQ(result.status, an::ui::CometReferenceResolutionStatus::Resolved) << result.message.toStdString();
    ASSERT_TRUE(result.resolvedReferenceFrame.has_value());
    EXPECT_EQ(result.resolvedReferenceFrame->canonicalPath(), frameB.canonicalPath());
}

TEST_F(TempDirFixture, CometReferenceResolutionAutoReferenceNeedsDetectionWhenMetricsMissing)
{
    const QString dbPath = metricsDbPath(tempPath());
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("light_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometReferenceMissingMetricsWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::CometReferenceResolutionService service(dbPath);
    an::ui::CometReferenceResolutionRequest request;
    request.groupKey = QStringLiteral("reg|comet|missing-reference-metrics");
    request.settingsVersion = 3;
    request.groupFrames = {frameA, frameB};
    request.autoReference = true;
    request.getFilter = [](const FrameId&) { return QStringLiteral("L"); };

    const auto result = service.resolve(request);
    EXPECT_EQ(result.status, an::ui::CometReferenceResolutionStatus::NeedsDetection);
    EXPECT_FALSE(result.resolvedReferenceFrame.has_value());
}

TEST_F(TempDirFixture, CometReferenceResolutionAutoReferenceUsesMetricsAndResolvedReferenceHash)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|auto-reference");
    constexpr int settingsVersion = 5;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("light_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometReferenceAutoWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(makeReferenceMetric(frameA, groupKey, settingsVersion, 80, 3.4, 18.0), &error))
        << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(makeReferenceMetric(frameB, groupKey, settingsVersion, 220, 1.8, 42.0), &error))
        << error.toStdString();

    an::ui::CometReferenceResolutionService service(dbPath);
    an::ui::CometReferenceResolutionRequest request;
    request.groupKey = groupKey;
    request.settingsVersion = settingsVersion;
    request.groupFrames = {frameA, frameB};
    request.autoReference = true;
    request.getFilter = [](const FrameId&) { return QStringLiteral("L"); };

    const auto result = service.resolve(request);
    ASSERT_EQ(result.status, an::ui::CometReferenceResolutionStatus::Resolved) << result.message.toStdString();
    ASSERT_TRUE(result.resolvedReferenceFrame.has_value());
    EXPECT_EQ(result.resolvedReferenceFrame->canonicalPath(), frameB.canonicalPath());

    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    RegistrationSettings::CometTrackingSettings comet;
    comet.observations = {
        RegistrationSettings::CometTrackingObservation{frameA.canonicalPath(), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{frameB.canonicalPath(), QPointF(20.0, 12.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    settings.cometTracking = comet;

    const QString frameAHash =
        ImageIntegration::RegistrationHashUtils::registrationTransformHash(settings, frameA.canonicalPath());
    const QString frameBHash =
        ImageIntegration::RegistrationHashUtils::registrationTransformHash(settings, frameB.canonicalPath());
    EXPECT_NE(frameAHash, frameBHash);
}

TEST_F(TempDirFixture, CometReferenceResolutionPreviewDoesNotMutateProductionRunRows)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|preview-readonly");
    constexpr int settingsVersion = 7;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("light_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometReferencePreviewWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(makeReferenceMetric(frameA, groupKey, settingsVersion, 140, 2.4, 25.0), &error))
        << error.toStdString();
    ASSERT_TRUE(writer.upsertMetric(makeReferenceMetric(frameB, groupKey, settingsVersion, 180, 2.1, 31.0), &error))
        << error.toStdString();

    core::SqliteConnection connection(dbPath, QStringLiteral("TestCometReferencePreviewInspect"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();
    for (const auto& tableName : stage_utils::cometSidecarTableNames())
    {
        ASSERT_EQ(tableRowCount(connection.db(), tableName), 0) << tableName.toStdString();
    }

    an::ui::CometReferenceResolutionService service(dbPath);
    an::ui::CometReferenceResolutionRequest request;
    request.groupKey = groupKey;
    request.settingsVersion = settingsVersion;
    request.groupFrames = {frameA, frameB};
    request.autoReference = true;
    request.getFilter = [](const FrameId&) { return QStringLiteral("L"); };

    auto result = service.resolve(request);
    ASSERT_EQ(result.status, an::ui::CometReferenceResolutionStatus::Resolved) << result.message.toStdString();

    request.autoReference = false;
    request.explicitReferenceFrame = frameA;
    result = service.resolve(request);
    ASSERT_EQ(result.status, an::ui::CometReferenceResolutionStatus::Resolved) << result.message.toStdString();

    for (const auto& tableName : stage_utils::cometSidecarTableNames())
    {
        EXPECT_EQ(tableRowCount(connection.db(), tableName), 0) << tableName.toStdString();
    }
}

TEST_F(TempDirFixture, CometReferenceResolutionReportsCancellation)
{
    const QString dbPath = metricsDbPath(tempPath());
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("light_a.fit")));

    an::ui::CometReferenceResolutionService service(dbPath);
    an::ui::CometReferenceResolutionRequest request;
    request.groupKey = QStringLiteral("reg|comet|cancel-reference");
    request.settingsVersion = 1;
    request.groupFrames = {frameA};
    request.autoReference = true;

    an::ui::CancellationToken token;
    token.requestCancel();

    const auto result = service.resolve(request, &token);
    EXPECT_EQ(result.status, an::ui::CometReferenceResolutionStatus::Cancelled);
    EXPECT_FALSE(result.resolvedReferenceFrame.has_value());
}

TEST_F(TempDirFixture, StandaloneRegistrationPersistenceRoundTripsGaiaAndRmsMetrics)
{
    const QString framePath = QDir(tempPath()).filePath(QStringLiteral("light.fit"));
    const FrameId frameId(framePath);
    const std::vector<FrameInfo> images{makeLightFrame(framePath, 640, 480)};

    RegisteredFrame image;
    image.sourcePath = framePath;
    image.stars = makeStars({QPointF(11.0, 12.0), QPointF(22.0, 24.0), QPointF(31.0, 37.0)});
    image.metrics.stars = static_cast<int>(image.stars.size());
    image.metrics.fwhm = 2.35;
    image.metrics.snr = 31.5;
    image.metrics.eccentricity = 0.08;
    image.metrics.photometric_zero_point = 21.85;
    image.metrics.zero_point_scatter = 0.14;
    image.metrics.astrometric_rms_pixels = 1.27;
    image.metrics.limiting_magnitude = 18.6;
    image.metrics.catalog_matched_stars = 37;
    // The reference's transform is the identity; its record carries the distortion the frame was registered over.
    image.transform = makeTranslationTransform(0.0, 0.0);
    image.distortion = stage_utils::SourceDistortionProvenance::explicitModel(makeSimpleSip());
    image.failed = false;

    RegisteredFrameSet registeredData;
    registeredData.referencePath = framePath;
    registeredData.frames.emplace(framePath, image);

    an::ui::StandaloneRegistrationPersistenceContext context;
    context.databasePath = metricsDbPath(tempPath());
    context.groupKey = QStringLiteral("standalone|roundtrip");
    context.settingsVersion = 0;
    context.detectionHash = QStringLiteral("det");
    context.registrationHash = QStringLiteral("reg");
    context.referenceFrameId = frameId.canonicalPath();
    context.lightFilesHash = QStringLiteral("lights");
    context.calibrationHash = QStringLiteral("cal");

    QString error;
    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(context, images, registeredData, &error))
        << error.toStdString();

    const auto loaded = an::ui::loadStandaloneRegistrationState(context, images, &error);
    ASSERT_TRUE(loaded.has_value()) << error.toStdString();

    const auto* metric = findMetricRecord(loaded->frameMetrics, framePath);
    ASSERT_NE(metric, nullptr);
    EXPECT_DOUBLE_EQ(metric->metrics.photometric_zero_point, image.metrics.photometric_zero_point);
    EXPECT_DOUBLE_EQ(metric->metrics.zero_point_scatter, image.metrics.zero_point_scatter);
    EXPECT_DOUBLE_EQ(metric->metrics.astrometric_rms_pixels, image.metrics.astrometric_rms_pixels);
    EXPECT_DOUBLE_EQ(metric->metrics.limiting_magnitude, image.metrics.limiting_magnitude);
    EXPECT_EQ(metric->metrics.catalog_matched_stars, image.metrics.catalog_matched_stars);

    const auto registeredIt = loaded->registeredImagesByFrameId.find(frameId.canonicalPath());
    ASSERT_NE(registeredIt, loaded->registeredImagesByFrameId.end());
    EXPECT_DOUBLE_EQ(registeredIt->second.metrics.photometric_zero_point, image.metrics.photometric_zero_point);
    EXPECT_DOUBLE_EQ(registeredIt->second.metrics.astrometric_rms_pixels, image.metrics.astrometric_rms_pixels);
    EXPECT_EQ(registeredIt->second.distortion.policy, stage_utils::SourceDistortionPolicy::ExplicitModel);
    ASSERT_TRUE(registeredIt->second.distortion.model.has_value());
    ASSERT_TRUE(image.distortion.model.has_value());
    EXPECT_DOUBLE_EQ(registeredIt->second.distortion.model->forwardX().coefficients[1][0],
                     image.distortion.model->forwardX().coefficients[1][0]);
}

TEST_F(TempDirFixture, RegistrationMetricsStateLoaderReconstructsPersistedRegistrationState)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|loader|reconstruct");
    const int settingsVersion = 11;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("reference.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("target.fit")));
    const QString inputA = QDir(tempPath()).filePath(QStringLiteral("calibrated/reference_cal.fit"));
    const QString inputB = QDir(tempPath()).filePath(QStringLiteral("calibrated/target_cal.fit"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestMetricsWriterLoad"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationGroupState groupState;
    groupState.groupKey = groupKey;
    groupState.settingsVersion = settingsVersion;
    groupState.detectionHash = QStringLiteral("det_loader");
    groupState.registrationHash = QStringLiteral("reg_loader");
    groupState.referenceFrameId = frameA.canonicalPath();
    groupState.lightFilesHash = QStringLiteral("light_hash_loader");
    groupState.calibrationHash = QStringLiteral("cal_hash_loader");
    groupState.outputWcsJson = QStringLiteral("{\"schemaVersion\":1,\"sourceKind\":\"reference_frame\"}");
    ASSERT_TRUE(writer.upsertGroupState(groupState, &error)) << error.toStdString();

    an::ui::RegistrationStarCacheEntry cacheA;
    cacheA.frameId = frameA;
    cacheA.groupKey = groupKey;
    cacheA.detectionHash = groupState.detectionHash;
    cacheA.inputPath = inputA;
    cacheA.inputMtime = 1700000001;
    cacheA.starsCount = 2;
    cacheA.fwhm = 2.0;
    cacheA.snr = 28.0;
    cacheA.roundness = 0.94;
    cacheA.starsBlob =
        an::ui::serializeRegistrationStarCacheBlob(makeStars({QPointF(11.0, 12.0), QPointF(22.0, 24.0)}));
    ASSERT_TRUE(writer.upsertStarCache(cacheA, &error)) << error.toStdString();

    an::ui::RegistrationStarCacheEntry cacheB;
    cacheB.frameId = frameB;
    cacheB.groupKey = groupKey;
    cacheB.detectionHash = groupState.detectionHash;
    cacheB.inputPath = inputB;
    cacheB.inputMtime = 1700000002;
    cacheB.starsCount = 1;
    cacheB.fwhm = 2.7;
    cacheB.snr = 18.0;
    cacheB.roundness = 0.81;
    cacheB.starsBlob = an::ui::serializeRegistrationStarCacheBlob(makeStars({QPointF(31.0, 17.0)}));
    ASSERT_TRUE(writer.upsertStarCache(cacheB, &error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metricA;
    metricA.frameId = frameA;
    metricA.groupKey = groupKey;
    metricA.settingsVersion = settingsVersion;
    metricA.metrics.stars = 2;
    metricA.metrics.fwhm = 2.0;
    metricA.metrics.snr = 28.0;
    metricA.metrics.eccentricity = 0.06;
    metricA.metrics.photometric_zero_point = 22.1;
    metricA.metrics.zero_point_scatter = 0.11;
    metricA.metrics.astrometric_rms_pixels = 0.0;
    metricA.metrics.astrometric_rms_arcsec = 0.0;
    metricA.metrics.limiting_magnitude = 18.9;
    metricA.metrics.catalog_matched_stars = 40;
    metricA.metrics.succeeded = true;
    metricA.outputPath = QDir(tempPath()).filePath(QStringLiteral("aligned/reference_aligned.fit"));
    metricA.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(0.0, 0.0));
    ASSERT_TRUE(writer.upsertMetric(metricA, &error)) << error.toStdString();

    const auto targetTransform = makeTranslationTransform(4.0, -3.5);
    const auto sourceSip = makeSimpleSip();
    an::ui::RegistrationMetricRecord metricB;
    metricB.frameId = frameB;
    metricB.groupKey = groupKey;
    metricB.settingsVersion = settingsVersion;
    metricB.metrics.stars = 1;
    metricB.metrics.fwhm = 2.7;
    metricB.metrics.snr = 18.0;
    metricB.metrics.eccentricity = 0.19;
    metricB.metrics.photometric_zero_point = 21.4;
    metricB.metrics.zero_point_scatter = 0.22;
    metricB.metrics.astrometric_rms_pixels = 1.6;
    metricB.metrics.astrometric_rms_arcsec = 0.63;
    metricB.metrics.limiting_magnitude = 17.5;
    metricB.metrics.catalog_matched_stars = 18;
    metricB.metrics.succeeded = true;
    metricB.outputPath = QDir(tempPath()).filePath(QStringLiteral("aligned/target_aligned.fit"));
    metricB.transformJson = stage_utils::serializeTransformJson(
        targetTransform, stage_utils::SourceDistortionProvenance::explicitModel(sourceSip));
    ASSERT_TRUE(writer.upsertMetric(metricB, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestMetricsLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    std::vector<an::ui::RegistrationMetricsGroupVersion> availableStates;
    ASSERT_TRUE(loader.loadAvailableStates(availableStates, &error)) << error.toStdString();
    ASSERT_EQ(availableStates.size(), 1u);
    EXPECT_EQ(availableStates.front().groupKey, groupKey);
    EXPECT_EQ(availableStates.front().settingsVersion, settingsVersion);

    const auto loadedState = loader.loadState(groupKey, settingsVersion, {}, &error);
    ASSERT_TRUE(loadedState.has_value()) << error.toStdString();
    ASSERT_TRUE(loadedState->groupState.has_value());

    EXPECT_EQ(loadedState->groupState->referenceFrameId, frameA.canonicalPath());
    EXPECT_EQ(loadedState->groupState->outputWcsJson, groupState.outputWcsJson);
    ASSERT_EQ(loadedState->frameMetrics.size(), 2u);
    ASSERT_EQ(loadedState->starCacheByFrame.size(), 2u);

    const auto* loadedMetricB = findMetricRecord(loadedState->frameMetrics, frameB.canonicalPath());
    ASSERT_NE(loadedMetricB, nullptr);
    EXPECT_DOUBLE_EQ(loadedMetricB->metrics.astrometric_rms_pixels, metricB.metrics.astrometric_rms_pixels);
    EXPECT_DOUBLE_EQ(loadedMetricB->metrics.astrometric_rms_arcsec, metricB.metrics.astrometric_rms_arcsec);
    EXPECT_EQ(loadedMetricB->outputPath, metricB.outputPath);

    const RegisteredFrameSet& registered = loadedState->registeredFrames;
    EXPECT_EQ(registered.referencePath, inputA);
    ASSERT_EQ(registered.frames.size(), 2u);

    const auto referenceIt = registered.frames.find(inputA);
    ASSERT_NE(referenceIt, registered.frames.end());
    EXPECT_FALSE(referenceIt->second.failed);
    EXPECT_EQ(referenceIt->second.stars.size(), 2u);
    ASSERT_TRUE(referenceIt->second.transform.has_value());
    EXPECT_NEAR(referenceIt->second.transform->translation().x, 0.0, 1e-9);
    EXPECT_NEAR(referenceIt->second.transform->translation().y, 0.0, 1e-9);

    const auto targetIt = registered.frames.find(inputB);
    ASSERT_NE(targetIt, registered.frames.end());
    EXPECT_FALSE(targetIt->second.failed);
    EXPECT_EQ(targetIt->second.stars.size(), 1u);
    EXPECT_DOUBLE_EQ(targetIt->second.metrics.astrometric_rms_pixels, metricB.metrics.astrometric_rms_pixels);
    ASSERT_TRUE(targetIt->second.transform.has_value());
    EXPECT_NEAR(targetIt->second.transform->translation().x, 4.0, 1e-9);
    EXPECT_NEAR(targetIt->second.transform->translation().y, -3.5, 1e-9);
    EXPECT_EQ(targetIt->second.distortion.policy, stage_utils::SourceDistortionPolicy::ExplicitModel);
    ASSERT_TRUE(targetIt->second.distortion.model.has_value());
    EXPECT_NEAR(targetIt->second.distortion.model->forwardX().coefficients[1][0],
                sourceSip.forwardX().coefficients[1][0], 1e-12);
    EXPECT_NEAR(targetIt->second.distortion.model->forwardY().coefficients[0][1],
                sourceSip.forwardY().coefficients[0][1], 1e-12);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterRoundTripsMatchedSourceHandoffReferences)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|handoff|roundtrip");
    const int settingsVersion = 13;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("handoff_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("handoff_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestMatchedSourceWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    auto satisfied = makeMatchedSourceReference(589012345678901234ULL);
    auto missingCache = makeMatchedSourceReference(589012345678901235ULL);
    missingCache.separationArcseconds = 0.91;
    missingCache.residualPixels = 0.74;

    ASSERT_TRUE(
        writer.replaceMatchedSourcesForFrame(groupKey, settingsVersion, frameA, {satisfied, missingCache}, &error))
        << error.toStdString();

    auto stale = makeMatchedSourceReference(589012345678901236ULL);
    stale.astrometricCatalogManifestId = "gaia-dr3-main-stale";
    ASSERT_TRUE(writer.upsertMatchedSource(
        matchedSourceRecord(frameB, groupKey, settingsVersion, stale, kLookupStaleMatch, 1700000300), &error))
        << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestMatchedSourceLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    std::vector<an::ui::RegistrationMatchedSourceRecord> records;
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, settingsVersion, records, &error)) << error.toStdString();
    ASSERT_EQ(records.size(), 3u);

    const auto* loadedSatisfied = findMatchedSourceRecord(records, frameA.canonicalPath(), satisfied.sourceIdentifier);
    ASSERT_NE(loadedSatisfied, nullptr);
    EXPECT_EQ(loadedSatisfied->groupKey, groupKey);
    EXPECT_EQ(loadedSatisfied->settingsVersion, settingsVersion);
    EXPECT_EQ(loadedSatisfied->reference.astrometricCatalogFamilyId, satisfied.astrometricCatalogFamilyId);
    EXPECT_EQ(loadedSatisfied->reference.astrometricCatalogManifestId, satisfied.astrometricCatalogManifestId);
    EXPECT_EQ(loadedSatisfied->reference.spectrumCatalogFamilyId, satisfied.spectrumCatalogFamilyId);
    EXPECT_EQ(loadedSatisfied->reference.spectrumCatalogManifestId, satisfied.spectrumCatalogManifestId);
    EXPECT_EQ(loadedSatisfied->reference.wcsSolutionId, satisfied.wcsSolutionId);
    EXPECT_EQ(loadedSatisfied->reference.observationEpoch, satisfied.observationEpoch);
    EXPECT_EQ(loadedSatisfied->reference.properMotionModelVersion, satisfied.properMotionModelVersion);
    ASSERT_TRUE(loadedSatisfied->reference.separationArcseconds.has_value());
    EXPECT_DOUBLE_EQ(*loadedSatisfied->reference.separationArcseconds, *satisfied.separationArcseconds);
    ASSERT_TRUE(loadedSatisfied->reference.residualPixels.has_value());
    EXPECT_DOUBLE_EQ(*loadedSatisfied->reference.residualPixels, *satisfied.residualPixels);
    // A frame's replacement records what registration matched; no calibration has looked a source up yet.
    EXPECT_EQ(loadedSatisfied->lookupStatus, QString::fromLatin1(an::ui::kMatchedSourceUntested));

    const auto* loadedMissing = findMatchedSourceRecord(records, frameA.canonicalPath(), missingCache.sourceIdentifier);
    ASSERT_NE(loadedMissing, nullptr);
    EXPECT_EQ(loadedMissing->lookupStatus, QString::fromLatin1(an::ui::kMatchedSourceUntested));
    ASSERT_TRUE(loadedMissing->reference.residualPixels.has_value());
    EXPECT_DOUBLE_EQ(*loadedMissing->reference.residualPixels, *missingCache.residualPixels);

    std::vector<an::ui::RegistrationMatchedSourceRecord> frameOnlyRecords;
    ASSERT_TRUE(loader.loadMatchedSourcesForFrame(groupKey, settingsVersion, frameB, frameOnlyRecords, &error))
        << error.toStdString();
    ASSERT_EQ(frameOnlyRecords.size(), 1u);
    EXPECT_EQ(frameOnlyRecords.front().reference.sourceIdentifier, stale.sourceIdentifier);
    EXPECT_EQ(frameOnlyRecords.front().lookupStatus, QString::fromLatin1(kLookupStaleMatch));

    const auto loadedState = loader.loadState(groupKey, settingsVersion, {}, &error);
    ASSERT_TRUE(loadedState.has_value()) << error.toStdString();
    ASSERT_EQ(loadedState->matchedSources.size(), 3u);
    EXPECT_NE(findMatchedSourceRecord(loadedState->matchedSources, frameB.canonicalPath(), stale.sourceIdentifier),
              nullptr);

    auto replacement = makeMatchedSourceReference(589012345678901237ULL);
    ASSERT_TRUE(writer.replaceMatchedSourcesForFrame(groupKey, settingsVersion, frameA, {replacement}, &error))
        << error.toStdString();

    records.clear();
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, settingsVersion, records, &error)) << error.toStdString();
    EXPECT_EQ(findMatchedSourceRecord(records, frameA.canonicalPath(), satisfied.sourceIdentifier), nullptr);
    EXPECT_EQ(findMatchedSourceRecord(records, frameA.canonicalPath(), missingCache.sourceIdentifier), nullptr);
    EXPECT_NE(findMatchedSourceRecord(records, frameA.canonicalPath(), replacement.sourceIdentifier), nullptr);
    EXPECT_NE(findMatchedSourceRecord(records, frameB.canonicalPath(), stale.sourceIdentifier), nullptr);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterDeleteAllForGroupClearsOnlyTargetGroup)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupA = QStringLiteral("reg|delete|groupA");
    const QString groupB = QStringLiteral("reg|delete|groupB");
    const int settingsVersion = 3;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestDeleteWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    // Write group state + metrics + star cache for groupA
    an::ui::RegistrationGroupState stateA;
    stateA.groupKey = groupA;
    stateA.settingsVersion = settingsVersion;
    stateA.detectionHash = QStringLiteral("detA");
    stateA.registrationHash = QStringLiteral("regA");
    stateA.referenceFrameId = frameA.canonicalPath();
    stateA.lightFilesHash = QStringLiteral("lightsA");
    stateA.calibrationHash = QStringLiteral("calA");
    ASSERT_TRUE(writer.upsertGroupState(stateA, &error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metricA;
    metricA.frameId = frameA;
    metricA.groupKey = groupA;
    metricA.settingsVersion = settingsVersion;
    metricA.metrics.stars = 80;
    metricA.metrics.fwhm = 2.5;
    metricA.metrics.succeeded = true;
    metricA.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(0.0, 0.0));
    ASSERT_TRUE(writer.upsertMetric(metricA, &error)) << error.toStdString();

    an::ui::RegistrationStarCacheEntry cacheA;
    cacheA.frameId = frameA;
    cacheA.groupKey = groupA;
    cacheA.detectionHash = stateA.detectionHash;
    cacheA.inputPath = frameA.canonicalPath();
    cacheA.inputMtime = 1700000010;
    cacheA.starsCount = 80;
    cacheA.starsBlob = an::ui::serializeRegistrationStarCacheBlob(makeStars({QPointF(5.0, 6.0)}));
    ASSERT_TRUE(writer.upsertStarCache(cacheA, &error)) << error.toStdString();

    ASSERT_TRUE(writer.upsertMatchedSource(matchedSourceRecord(frameA, groupA, settingsVersion,
                                                               makeMatchedSourceReference(589012345678902001ULL),
                                                               kLookupSatisfied, 1700000400),
                                           &error))
        << error.toStdString();

    // Write group state + metrics for groupB (should survive the delete)
    an::ui::RegistrationGroupState stateB;
    stateB.groupKey = groupB;
    stateB.settingsVersion = settingsVersion;
    stateB.detectionHash = QStringLiteral("detB");
    stateB.registrationHash = QStringLiteral("regB");
    stateB.referenceFrameId = frameB.canonicalPath();
    stateB.lightFilesHash = QStringLiteral("lightsB");
    stateB.calibrationHash = QStringLiteral("calB");
    ASSERT_TRUE(writer.upsertGroupState(stateB, &error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metricB;
    metricB.frameId = frameB;
    metricB.groupKey = groupB;
    metricB.settingsVersion = settingsVersion;
    metricB.metrics.stars = 55;
    metricB.metrics.fwhm = 3.1;
    metricB.metrics.succeeded = true;
    metricB.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(1.0, 2.0));
    ASSERT_TRUE(writer.upsertMetric(metricB, &error)) << error.toStdString();

    ASSERT_TRUE(writer.upsertMatchedSource(matchedSourceRecord(frameB, groupB, settingsVersion,
                                                               makeMatchedSourceReference(589012345678902002ULL),
                                                               kLookupSatisfied, 1700000401),
                                           &error))
        << error.toStdString();

    // Delete groupA
    ASSERT_TRUE(writer.deleteAllForGroup(groupA, &error)) << error.toStdString();

    // Verify groupA is gone
    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestDeleteLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    std::vector<an::ui::RegistrationMetricsGroupVersion> available;
    ASSERT_TRUE(loader.loadAvailableStates(available, &error)) << error.toStdString();
    ASSERT_EQ(available.size(), 1u);
    EXPECT_EQ(available.front().groupKey, groupB);

    // Verify groupB metrics are intact
    std::vector<an::ui::RegistrationMetricRecord> recordsB;
    ASSERT_TRUE(loader.loadMetricRecords(groupB, settingsVersion, recordsB, &error)) << error.toStdString();
    ASSERT_EQ(recordsB.size(), 1u);
    EXPECT_EQ(recordsB.front().metrics.stars, metricB.metrics.stars);
    EXPECT_DOUBLE_EQ(recordsB.front().metrics.fwhm, metricB.metrics.fwhm);

    std::vector<an::ui::RegistrationMatchedSourceRecord> matchedA;
    ASSERT_TRUE(loader.loadMatchedSources(groupA, settingsVersion, matchedA, &error)) << error.toStdString();
    EXPECT_TRUE(matchedA.empty());

    std::vector<an::ui::RegistrationMatchedSourceRecord> matchedB;
    ASSERT_TRUE(loader.loadMatchedSources(groupB, settingsVersion, matchedB, &error)) << error.toStdString();
    ASSERT_EQ(matchedB.size(), 1u);
    EXPECT_EQ(matchedB.front().frameId.canonicalPath(), frameB.canonicalPath());
}

TEST_F(TempDirFixture, RegistrationMetricsWriterCopyMetricsBulkCopiesAndPreservesSource)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|copy|group");
    const int versionFrom = 5;
    const int versionTo = 6;
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("copy_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("copy_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCopyWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metricA;
    metricA.frameId = frameA;
    metricA.groupKey = groupKey;
    metricA.settingsVersion = versionFrom;
    metricA.metrics.stars = 120;
    metricA.metrics.fwhm = 1.8;
    metricA.metrics.snr = 40.0;
    metricA.metrics.eccentricity = 0.05;
    metricA.metrics.succeeded = true;
    metricA.outputPath = QDir(tempPath()).filePath(QStringLiteral("out/a_aligned.fit"));
    metricA.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(0.0, 0.0));
    ASSERT_TRUE(writer.upsertMetric(metricA, &error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metricB;
    metricB.frameId = frameB;
    metricB.groupKey = groupKey;
    metricB.settingsVersion = versionFrom;
    metricB.metrics.stars = 75;
    metricB.metrics.fwhm = 2.3;
    metricB.metrics.snr = 22.0;
    metricB.metrics.eccentricity = 0.12;
    metricB.metrics.succeeded = true;
    metricB.outputPath = QDir(tempPath()).filePath(QStringLiteral("out/b_aligned.fit"));
    metricB.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(2.5, -1.0));
    ASSERT_TRUE(writer.upsertMetric(metricB, &error)) << error.toStdString();

    const auto sourceReference = makeMatchedSourceReference(589012345678903001ULL);
    ASSERT_TRUE(writer.upsertMatchedSource(
        matchedSourceRecord(frameA, groupKey, versionFrom, sourceReference, kLookupSatisfied, 1700000500), &error))
        << error.toStdString();

    // noop: from == to should succeed immediately without touching the DB
    ASSERT_TRUE(writer.copyMetricsToSettingsVersion(groupKey, versionFrom, versionFrom, &error)) << error.toStdString();

    // Copy versionFrom → versionTo
    ASSERT_TRUE(writer.copyMetricsToSettingsVersion(groupKey, versionFrom, versionTo, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCopyLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    // Both versions should now exist
    std::vector<an::ui::RegistrationMetricsGroupVersion> available;
    ASSERT_TRUE(loader.loadAvailableStates(available, &error)) << error.toStdString();
    const auto hasVersion = [&](int v) {
        return std::any_of(available.begin(), available.end(),
                           [&](const auto& s) { return s.groupKey == groupKey && s.settingsVersion == v; });
    };
    EXPECT_TRUE(hasVersion(versionFrom));
    EXPECT_TRUE(hasVersion(versionTo));

    // Copied rows must have same metric values as the originals
    std::vector<an::ui::RegistrationMetricRecord> copiedRecords;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, versionTo, copiedRecords, &error)) << error.toStdString();
    ASSERT_EQ(copiedRecords.size(), 2u);

    const auto* copiedA = findMetricRecord(copiedRecords, frameA.canonicalPath());
    const auto* copiedB = findMetricRecord(copiedRecords, frameB.canonicalPath());
    ASSERT_NE(copiedA, nullptr);
    ASSERT_NE(copiedB, nullptr);

    EXPECT_EQ(copiedA->metrics.stars, metricA.metrics.stars);
    EXPECT_DOUBLE_EQ(copiedA->metrics.fwhm, metricA.metrics.fwhm);
    EXPECT_EQ(copiedA->outputPath, metricA.outputPath);

    EXPECT_EQ(copiedB->metrics.stars, metricB.metrics.stars);
    EXPECT_DOUBLE_EQ(copiedB->metrics.snr, metricB.metrics.snr);
    EXPECT_EQ(copiedB->outputPath, metricB.outputPath);

    // Source version must still exist unchanged
    std::vector<an::ui::RegistrationMetricRecord> sourceRecords;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, versionFrom, sourceRecords, &error)) << error.toStdString();
    ASSERT_EQ(sourceRecords.size(), 2u);
    const auto* sourceA = findMetricRecord(sourceRecords, frameA.canonicalPath());
    ASSERT_NE(sourceA, nullptr);
    EXPECT_EQ(sourceA->metrics.stars, metricA.metrics.stars);

    std::vector<an::ui::RegistrationMatchedSourceRecord> copiedMatchedSources;
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, versionTo, copiedMatchedSources, &error)) << error.toStdString();
    ASSERT_EQ(copiedMatchedSources.size(), 1u);
    EXPECT_EQ(copiedMatchedSources.front().frameId.canonicalPath(), frameA.canonicalPath());
    EXPECT_EQ(copiedMatchedSources.front().reference.sourceIdentifier, sourceReference.sourceIdentifier);
    EXPECT_EQ(copiedMatchedSources.front().lookupStatus, QString::fromLatin1(kLookupSatisfied));

    std::vector<an::ui::RegistrationMatchedSourceRecord> sourceMatchedSources;
    ASSERT_TRUE(loader.loadMatchedSources(groupKey, versionFrom, sourceMatchedSources, &error)) << error.toStdString();
    ASSERT_EQ(sourceMatchedSources.size(), 1u);
    EXPECT_EQ(sourceMatchedSources.front().reference.sourceIdentifier, sourceReference.sourceIdentifier);
}

TEST_F(TempDirFixture, RegistrationMetricsWriterCopyMetricsDoesNotCloneGeneratedCometSidecars)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|copy|comet-sidecars");
    const int versionFrom = 8;
    const int versionTo = 9;
    const FrameId frame(QDir(tempPath()).filePath(QStringLiteral("copy_comet.fit")));
    const QString runId = QStringLiteral("copy-comet-run-source-version");

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCopyCometSidecarWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    ASSERT_TRUE(writer.upsertMetric(makeReferenceMetric(frame, groupKey, versionFrom, 140, 2.1, 34.0), &error))
        << error.toStdString();

    auto run = makeCometRun(runId, groupKey, frame, QStringLiteral("Active"));
    run.settingsVersion = versionFrom;
    run.registrationHash = QStringLiteral("reg-copy-comet");
    run.lightFilesHash = QStringLiteral("lights-copy-comet");
    run.calibrationHash = QStringLiteral("cal-copy-comet");
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();

    auto plane = makeCometPlane(runId);
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();

    an::ui::CometTrackingRunSummaryRecord summary;
    summary.cometRunId = runId;
    summary.fitModel = QStringLiteral("linear");
    summary.acceptedInputCount = 2;
    ASSERT_TRUE(writer.upsertCometTrackingRunSummary(summary, &error)) << error.toStdString();

    an::ui::CometTrackingPointRecord point;
    point.pointId = QStringLiteral("copy-comet-point");
    point.cometRunId = runId;
    point.originalFrameId = frame;
    point.origin = QStringLiteral("Manual");
    point.fitState = QStringLiteral("Inlier");
    point.measurementMethod = QStringLiteral("manual");
    ASSERT_TRUE(writer.upsertCometTrackingPoint(point, &error)) << error.toStdString();

    an::ui::CometTransformRoleRecord transform;
    transform.cometRunId = runId;
    transform.frameId = frame;
    transform.role = QStringLiteral("comet_aligned");
    transform.cometOutputPlaneId = plane.cometOutputPlaneId;
    transform.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(1.0, 2.0));
    transform.succeeded = true;
    ASSERT_TRUE(writer.upsertCometTransformRole(transform, &error)) << error.toStdString();

    an::ui::CometRegisteredOutputRecord output;
    output.cometRunId = runId;
    output.frameId = frame;
    output.role = QStringLiteral("comet_aligned");
    output.cometOutputPlaneId = plane.cometOutputPlaneId;
    output.outputPath = QDir(tempPath()).filePath(QStringLiteral("copy-comet-registered.fit"));
    output.artifactHash = QStringLiteral("copy-comet-registered-hash");
    output.succeeded = true;
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(output, &error)) << error.toStdString();

    ASSERT_TRUE(writer.upsertCometRoleReadiness(makeCometReadiness(runId, QStringLiteral("Ready"), true, 1), &error))
        << error.toStdString();
    ASSERT_TRUE(writer.upsertStackingCometArtifact(
        makeStackingCometArtifact(runId, plane.cometOutputPlaneId, QStringLiteral("FinalComposite"),
                                  QDir(tempPath()).filePath(QStringLiteral("copy-comet-output.fit")),
                                  QStringLiteral("copy-comet-artifact-hash")),
        &error))
        << error.toStdString();

    ASSERT_TRUE(writer.copyMetricsToSettingsVersion(groupKey, versionFrom, versionTo, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCopyCometSidecarLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    std::vector<an::ui::RegistrationMetricRecord> copiedRecords;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, versionTo, copiedRecords, &error)) << error.toStdString();
    ASSERT_EQ(copiedRecords.size(), 1u);
    EXPECT_EQ(copiedRecords.front().frameId.canonicalPath(), frame.canonicalPath());

    EXPECT_EQ(cometRunCountForSettingsVersion(dbPath, groupKey, versionFrom), 1);
    EXPECT_EQ(cometRunCountForSettingsVersion(dbPath, groupKey, versionTo), 0);

    const auto linkedRun = loader.loadActiveCometRunForContext(groupKey, run.registrationHash, run.lightFilesHash,
                                                               run.calibrationHash, frame.canonicalPath(), &error);
    ASSERT_TRUE(linkedRun.has_value()) << error.toStdString();
    EXPECT_EQ(linkedRun->cometRunId, runId);
    EXPECT_EQ(linkedRun->settingsVersion, versionFrom);

    struct ActiveRunLookup
    {
        QString groupKey;
        QString registrationHash;
        QString lightFilesHash;
        QString calibrationHash;
        QString referenceFrameId;
    };
    const std::vector<ActiveRunLookup> mismatches = {
        {QStringLiteral("reg|copy|comet-sidecars-other"), run.registrationHash, run.lightFilesHash, run.calibrationHash,
         frame.canonicalPath()},
        {groupKey, QStringLiteral("reg-copy-comet-mismatched"), run.lightFilesHash, run.calibrationHash,
         frame.canonicalPath()},
        {groupKey, run.registrationHash, QStringLiteral("lights-copy-comet-mismatched"), run.calibrationHash,
         frame.canonicalPath()},
        {groupKey, run.registrationHash, run.lightFilesHash, QStringLiteral("cal-copy-comet-mismatched"),
         frame.canonicalPath()},
        {groupKey, run.registrationHash, run.lightFilesHash, run.calibrationHash,
         FrameId(QDir(tempPath()).filePath(QStringLiteral("copy_comet_other_ref.fit"))).canonicalPath()}};
    for (const auto& lookup : mismatches)
    {
        const auto mismatchedRun =
            loader.loadActiveCometRunForContext(lookup.groupKey, lookup.registrationHash, lookup.lightFilesHash,
                                                lookup.calibrationHash, lookup.referenceFrameId, &error);
        EXPECT_FALSE(mismatchedRun.has_value());
    }

    const auto sourceSummary = loader.loadCometTrackingRunSummary(runId, &error);
    ASSERT_TRUE(sourceSummary.has_value()) << error.toStdString();
    EXPECT_EQ(sourceSummary->fitModel, QStringLiteral("linear"));

    std::vector<an::ui::CometTrackingPointRecord> sourcePoints;
    ASSERT_TRUE(loader.loadCometTrackingPoints(runId, sourcePoints, &error)) << error.toStdString();
    ASSERT_EQ(sourcePoints.size(), 1u);

    std::vector<an::ui::CometTransformRoleRecord> sourceTransforms;
    ASSERT_TRUE(loader.loadCometTransformRoles(runId, QStringLiteral("comet_aligned"), sourceTransforms, &error))
        << error.toStdString();
    ASSERT_EQ(sourceTransforms.size(), 1u);

    EXPECT_TRUE(loader.loadCometRegisteredOutput(runId, frame, QStringLiteral("comet_aligned"), &error).has_value())
        << error.toStdString();

    std::vector<an::ui::StackingCometArtifactRecord> sourceArtifacts;
    ASSERT_TRUE(loader.loadStackingCometArtifacts(runId, sourceArtifacts, &error)) << error.toStdString();
    ASSERT_EQ(sourceArtifacts.size(), 1u);
    EXPECT_EQ(sourceArtifacts.front().artifactHash, QStringLiteral("copy-comet-artifact-hash"));
}

TEST_F(TempDirFixture, RegistrationMetricsSchemaIncludesValidatedCometSidecarTables)
{
    const QString dbPath = metricsDbPath(tempPath());

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometSchemaWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    core::SqliteConnection connection(dbPath, QStringLiteral("TestCometSchemaInspect"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    const auto inspection = stage_utils::inspectRegistrationMetricsSchema(connection.db());
    EXPECT_TRUE(inspection.isCurrent());
    EXPECT_TRUE(inspection.cometRunsSchemaOk);
    EXPECT_TRUE(inspection.cometRunSummarySchemaOk);
    EXPECT_TRUE(inspection.cometPointsSchemaOk);
    EXPECT_TRUE(inspection.cometTransformRolesSchemaOk);
    EXPECT_TRUE(inspection.cometOutputPlanesSchemaOk);
    EXPECT_TRUE(inspection.cometRegisteredOutputsSchemaOk);
    EXPECT_TRUE(inspection.cometRoleReadinessSchemaOk);
    EXPECT_TRUE(inspection.stackingCometArtifactsSchemaOk);

    for (const auto& tableName : stage_utils::cometSidecarTableNames())
    {
        EXPECT_EQ(tableRowCount(connection.db(), tableName), 0) << tableName.toStdString();
    }
}

TEST_F(TempDirFixture, RegistrationMetricsSchemaInspectionRejectsCometTablesWithoutEnumConstraints)
{
    const QString dbPath = metricsDbPath(tempPath());

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestBrokenCometSchemaWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    core::SqliteConnection connection(dbPath, QStringLiteral("TestBrokenCometSchemaInspect"));
    ASSERT_TRUE(connection.open(&error)) << error.toStdString();

    QSqlQuery query(connection.db());
    ASSERT_TRUE(query.exec(QStringLiteral("DROP TABLE comet_tracking_runs")));
    ASSERT_TRUE(query.exec(QStringLiteral("CREATE TABLE comet_tracking_runs ("
                                          "comet_run_id TEXT NOT NULL PRIMARY KEY,"
                                          "group_key TEXT NOT NULL,"
                                          "settings_version INTEGER NOT NULL,"
                                          "comet_context_id TEXT NOT NULL DEFAULT '',"
                                          "detection_hash TEXT NOT NULL DEFAULT '',"
                                          "registration_hash TEXT NOT NULL,"
                                          "light_files_hash TEXT NOT NULL,"
                                          "calibration_hash TEXT NOT NULL DEFAULT '',"
                                          "resolved_reference_frame_id TEXT NOT NULL DEFAULT '',"
                                          "reference_epoch REAL,"
                                          "mode TEXT NOT NULL,"
                                          "status TEXT NOT NULL,"
                                          "created_at INTEGER NOT NULL,"
                                          "updated_at INTEGER NOT NULL)")));

    const auto inspection = stage_utils::inspectRegistrationMetricsSchema(connection.db());
    EXPECT_FALSE(inspection.isCurrent());
    EXPECT_FALSE(inspection.requiresReset());
    EXPECT_FALSE(inspection.cometRunsSchemaOk);
    // Tables that are not the stated format's are refused like another format: never read as this version's.
    const QString refusal = stage_utils::registrationMetricsFormatRefusal(inspection, dbPath);
    EXPECT_TRUE(refusal.contains(QStringLiteral("do not hold that format's tables"))) << refusal.toStdString();
}

TEST_F(TempDirFixture, CometSidecarRowsRoundTripWithoutOrdinaryMetrics)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|sidecar");
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("comet_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("comet_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometSidecarWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::CometTrackingRunRecord run;
    run.cometRunId = QStringLiteral("comet-run-001");
    run.groupKey = groupKey;
    run.settingsVersion = 9;
    run.cometContextId = QStringLiteral("ctx-001");
    run.detectionHash = QStringLiteral("det-001");
    run.registrationHash = QStringLiteral("reg-001");
    run.lightFilesHash = QStringLiteral("lights-001");
    run.calibrationHash = QStringLiteral("cal-001");
    run.resolvedReferenceFrameId = frameA.canonicalPath();
    run.referenceEpoch = 2460000.5;
    run.mode = QStringLiteral("PureComet");
    run.status = QStringLiteral("Active");
    run.createdAt = 100;
    run.updatedAt = 110;
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();

    an::ui::CometTrackingRunSummaryRecord summary;
    summary.cometRunId = run.cometRunId;
    summary.fitModel = QStringLiteral("linear_xy");
    summary.referenceEpoch = run.referenceEpoch;
    summary.fitTimeOrigin = 2460000.0;
    summary.rmsPixels = 0.42;
    summary.maxResidualPixels = 0.8;
    summary.acceptedInputCount = 2;
    summary.predictedOnlyFrameCount = 1;
    summary.warningFlags = QStringLiteral("predicted_only");
    summary.updatedAt = 120;
    ASSERT_TRUE(writer.upsertCometTrackingRunSummary(summary, &error)) << error.toStdString();

    an::ui::CometTrackingPointRecord point;
    point.pointId = QStringLiteral("point-a");
    point.cometRunId = run.cometRunId;
    point.originalFrameId = frameA;
    point.refinementSourcePath = frameA.canonicalPath();
    point.epoch = 2460000.5;
    point.sourcePosition = QPointF(101.25, 202.5);
    point.referencePlanePosition = QPointF(104.0, 205.0);
    point.confidence = 0.95;
    point.residualPixels = 0.12;
    point.apertureRadiusPixels = 8.0;
    point.snr = 27.0;
    point.measurementMethod = QStringLiteral("manual");
    point.origin = QStringLiteral("Manual");
    point.fitState = QStringLiteral("Inlier");
    point.updatedAt = 130;
    ASSERT_TRUE(writer.upsertCometTrackingPoint(point, &error)) << error.toStdString();

    an::ui::CometOutputPlaneRecord plane;
    plane.cometOutputPlaneId = QStringLiteral("plane-comet-001");
    plane.cometRunId = run.cometRunId;
    plane.role = QStringLiteral("comet_aligned");
    plane.alignmentMode = QStringLiteral("PureComet");
    plane.coordinateFrameKind = QStringLiteral("PixelOnly");
    plane.canvasWidth = 640;
    plane.canvasHeight = 480;
    plane.skyWcsValid = false;
    plane.skyWcsInvalidReason = QStringLiteral("comet_aligned is a non-sidereal moving-object plane");
    plane.descriptorJson = QStringLiteral("{\"schemaVersion\":1}");
    plane.sourceFingerprint = QStringLiteral("plane-fp-001");
    plane.updatedAt = 140;
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();

    an::ui::CometTransformRoleRecord transform;
    transform.cometRunId = run.cometRunId;
    transform.frameId = frameB;
    transform.role = QStringLiteral("comet_aligned");
    transform.cometOutputPlaneId = plane.cometOutputPlaneId;
    transform.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(12.0, -5.0));
    transform.succeeded = true;
    transform.updatedAt = 150;
    ASSERT_TRUE(writer.upsertCometTransformRole(transform, &error)) << error.toStdString();

    an::ui::CometRegisteredOutputRecord output;
    output.cometRunId = run.cometRunId;
    output.frameId = frameB;
    output.role = QStringLiteral("comet_aligned");
    output.cometOutputPlaneId = plane.cometOutputPlaneId;
    output.outputPath = QDir(tempPath()).filePath(QStringLiteral("comet_registered/comet_b.fit"));
    output.artifactHash = QStringLiteral("artifact-001");
    output.succeeded = true;
    output.updatedAt = 160;
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(output, &error)) << error.toStdString();

    an::ui::CometRoleReadinessRecord readiness;
    readiness.cometRunId = run.cometRunId;
    readiness.role = QStringLiteral("comet_aligned");
    readiness.readinessStatus = QStringLiteral("TransformReady");
    readiness.selectedFrameCount = 2;
    readiness.successfulTransformCount = 2;
    readiness.outputPlaneStatus = QStringLiteral("Ready");
    readiness.updatedAt = 170;
    ASSERT_TRUE(writer.upsertCometRoleReadiness(readiness, &error)) << error.toStdString();

    an::ui::StackingCometArtifactRecord artifact;
    artifact.cometRunId = run.cometRunId;
    artifact.integrationGroupKey = QStringLiteral("integration|comet|sidecar");
    artifact.stackingHash = QStringLiteral("stack-001");
    artifact.starBaseOutputDescriptorId = QStringLiteral("none");
    artifact.cometOutputPlaneId = plane.cometOutputPlaneId;
    artifact.artifactKind = QStringLiteral("CometLayer");
    artifact.outputPath = QDir(tempPath()).filePath(QStringLiteral("stack/comet_layer.fit"));
    artifact.artifactHash = QStringLiteral("stack-artifact-001");
    artifact.maskSettingsFingerprint = QStringLiteral("mask-fp-001");
    artifact.succeeded = true;
    artifact.updatedAt = 180;
    ASSERT_TRUE(writer.upsertStackingCometArtifact(artifact, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCometSidecarLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    const auto activeRun = loader.loadActiveCometRunForContext(
        groupKey, run.registrationHash, run.lightFilesHash, run.calibrationHash, run.resolvedReferenceFrameId, &error);
    ASSERT_TRUE(activeRun.has_value()) << error.toStdString();
    EXPECT_EQ(activeRun->cometRunId, run.cometRunId);
    EXPECT_EQ(activeRun->mode, QStringLiteral("PureComet"));

    const auto loadedReadiness = loader.loadCometRoleReadiness(run.cometRunId, QStringLiteral("comet_aligned"), &error);
    ASSERT_TRUE(loadedReadiness.has_value()) << error.toStdString();
    EXPECT_EQ(loadedReadiness->readinessStatus, QStringLiteral("TransformReady"));
    EXPECT_FALSE(loadedReadiness->requiresMaterializedOutputs);

    const auto loadedPlane = loader.loadCometOutputPlane(plane.cometOutputPlaneId, &error);
    ASSERT_TRUE(loadedPlane.has_value()) << error.toStdString();
    EXPECT_EQ(loadedPlane->coordinateFrameKind, QStringLiteral("PixelOnly"));
    EXPECT_FALSE(loadedPlane->skyWcsValid);
    EXPECT_FALSE(an::ui::isCometOutputPlaneSiderealWcsUsable(*loadedPlane));

    const auto loadedTransform =
        loader.loadCometTransformRole(run.cometRunId, frameB, QStringLiteral("comet_aligned"), &error);
    ASSERT_TRUE(loadedTransform.has_value()) << error.toStdString();
    EXPECT_TRUE(loadedTransform->succeeded);
    EXPECT_EQ(loadedTransform->cometOutputPlaneId, plane.cometOutputPlaneId);

    const auto loadedOutput =
        loader.loadCometRegisteredOutput(run.cometRunId, frameB, QStringLiteral("comet_aligned"), &error);
    ASSERT_TRUE(loadedOutput.has_value()) << error.toStdString();
    EXPECT_EQ(loadedOutput->outputPath, output.outputPath);

    std::vector<an::ui::StackingCometArtifactRecord> artifacts;
    ASSERT_TRUE(loader.loadStackingCometArtifacts(run.cometRunId, artifacts, &error)) << error.toStdString();
    ASSERT_EQ(artifacts.size(), 1u);
    EXPECT_EQ(artifacts.front().artifactKind, QStringLiteral("CometLayer"));

    std::vector<an::ui::RegistrationMetricRecord> ordinaryMetrics;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, run.settingsVersion, ordinaryMetrics, &error))
        << error.toStdString();
    EXPECT_TRUE(ordinaryMetrics.empty());

    core::SqliteConnection duplicateConnection(dbPath, QStringLiteral("TestCometDuplicateKey"));
    ASSERT_TRUE(duplicateConnection.open(&error)) << error.toStdString();
    EXPECT_FALSE(insertDuplicateCometTransform(duplicateConnection.db(), transform));
}

TEST_F(TempDirFixture, CometSidecarProductReaderReportsMissingRoleProductWithoutOrdinaryFallback)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|missing-product-reader");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("reader_missing_reference.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometMissingProductReaderWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::CometTrackingRunRecord run;
    run.cometRunId = QStringLiteral("comet-run-missing-product-reader");
    run.groupKey = groupKey;
    run.settingsVersion = 7;
    run.registrationHash = QStringLiteral("reg-missing-product-reader");
    run.lightFilesHash = QStringLiteral("lights-missing-product-reader");
    run.calibrationHash = QStringLiteral("cal-missing-product-reader");
    run.resolvedReferenceFrameId = referenceFrame.canonicalPath();
    run.mode = QStringLiteral("PureComet");
    run.status = QStringLiteral("RegistrationReady");
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCometMissingProductReaderLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    error.clear();
    const auto product = loader.loadCometTransformRoleProduct(run.cometRunId, QStringLiteral("comet_aligned"), &error);
    EXPECT_FALSE(product.has_value());
    EXPECT_TRUE(error.contains(QStringLiteral("readiness"), Qt::CaseInsensitive)) << error.toStdString();

    std::vector<an::ui::RegistrationMetricRecord> ordinaryMetrics;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, run.settingsVersion, ordinaryMetrics, &error))
        << error.toStdString();
    EXPECT_TRUE(ordinaryMetrics.empty());
}

TEST_F(TempDirFixture, CometStackingProductResolverRejectsStarOnlyRegisteredOutputsWithoutActiveCometRun)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|stacking-no-fallback");
    const int settingsVersion = 8;
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("stacking_reference.fit")));
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("stacking_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("stacking_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometStackNoFallbackWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    auto metricA = makeReferenceMetric(frameA, groupKey, settingsVersion, 120, 2.4, 28.0);
    metricA.outputPath = QDir(tempPath()).filePath(QStringLiteral("star_aligned/stacking_a.fit"));
    writePlaceholderFile(metricA.outputPath);
    ASSERT_TRUE(writer.upsertMetric(metricA, &error)) << error.toStdString();

    auto metricB = makeReferenceMetric(frameB, groupKey, settingsVersion, 118, 2.5, 27.0);
    metricB.outputPath = QDir(tempPath()).filePath(QStringLiteral("star_aligned/stacking_b.fit"));
    writePlaceholderFile(metricB.outputPath);
    ASSERT_TRUE(writer.upsertMetric(metricB, &error)) << error.toStdString();

    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = dbPath;
    request.groupKey = groupKey;
    request.registrationHash = QStringLiteral("reg-no-active-comet-run");
    request.lightFilesHash = QStringLiteral("lights-no-active-comet-run");
    request.calibrationHash = QStringLiteral("cal-no-active-comet-run");
    request.resolvedReferenceFrameId = referenceFrame.canonicalPath();
    request.stackingMode = QStringLiteral("Comet");
    request.drizzle = false;
    request.frameIds = {frameA, frameB};

    const auto result = an::ui::resolveCometStackingProducts(request);
    EXPECT_FALSE(result.available);
    EXPECT_EQ(result.kind, an::ui::CometStackingProductKind::None);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("active comet tracking run"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();
    EXPECT_TRUE(result.registeredOutputs.empty());
    EXPECT_TRUE(result.transforms.empty());
}

TEST_F(TempDirFixture, CometStackingProductResolverNonDrizzleUsesCometRegisteredOutputs)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|stacking-outputs");
    const int settingsVersion = 9;
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("stacking_output_reference.fit")));
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("stacking_output_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("stacking_output_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometStackOutputsWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    auto starMetric = makeReferenceMetric(frameA, groupKey, settingsVersion, 122, 2.3, 29.0);
    starMetric.outputPath = QDir(tempPath()).filePath(QStringLiteral("star_aligned/not_used_a.fit"));
    writePlaceholderFile(starMetric.outputPath);
    ASSERT_TRUE(writer.upsertMetric(starMetric, &error)) << error.toStdString();

    const auto run =
        makeCometRun(QStringLiteral("run-stacking-outputs"), groupKey, referenceFrame, QStringLiteral("Active"));
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();
    const auto plane = makeCometPlane(run.cometRunId);
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();
    ASSERT_TRUE(
        writer.upsertCometRoleReadiness(makeCometReadiness(run.cometRunId, QStringLiteral("Ready"), true), &error))
        << error.toStdString();

    an::ui::CometRegisteredOutputRecord outputA;
    outputA.cometRunId = run.cometRunId;
    outputA.frameId = frameA;
    outputA.role = QStringLiteral("comet_aligned");
    outputA.cometOutputPlaneId = plane.cometOutputPlaneId;
    outputA.outputPath = QDir(tempPath()).filePath(QStringLiteral("comet_aligned/stacking_output_a.fit"));
    outputA.artifactHash = QStringLiteral("comet-output-a");
    outputA.succeeded = true;
    writePlaceholderFile(outputA.outputPath);
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(outputA, &error)) << error.toStdString();

    an::ui::CometRegisteredOutputRecord outputB = outputA;
    outputB.frameId = frameB;
    outputB.outputPath = QDir(tempPath()).filePath(QStringLiteral("comet_aligned/stacking_output_b.fit"));
    outputB.artifactHash = QStringLiteral("comet-output-b");
    writePlaceholderFile(outputB.outputPath);
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(outputB, &error)) << error.toStdString();

    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = dbPath;
    request.groupKey = groupKey;
    request.registrationHash = run.registrationHash;
    request.lightFilesHash = run.lightFilesHash;
    request.calibrationHash = run.calibrationHash;
    request.resolvedReferenceFrameId = run.resolvedReferenceFrameId;
    request.stackingMode = QStringLiteral("Comet");
    request.drizzle = false;
    request.frameIds = {frameA, frameB};

    const auto result = an::ui::resolveCometStackingProducts(request);
    ASSERT_TRUE(result.available) << result.errorMessage.toStdString();
    EXPECT_EQ(result.kind, an::ui::CometStackingProductKind::RegisteredOutputs);
    EXPECT_EQ(result.cometRunId, run.cometRunId);
    ASSERT_TRUE(result.outputPlane.has_value());
    EXPECT_EQ(result.outputPlane->cometOutputPlaneId, plane.cometOutputPlaneId);
    ASSERT_EQ(result.registeredOutputs.size(), 2U);
    EXPECT_EQ(result.registeredOutputs[0].outputPath, outputA.outputPath);
    EXPECT_EQ(result.registeredOutputs[1].outputPath, outputB.outputPath);
    EXPECT_NE(result.registeredOutputs[0].outputPath, starMetric.outputPath);
    EXPECT_TRUE(result.transforms.empty());

    QFile replacement(outputB.outputPath);
    ASSERT_TRUE(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(replacement.write("replacement-comet-generation"), qint64(28));
    replacement.close();

    const auto staleResult = an::ui::resolveCometStackingProducts(request);
    EXPECT_FALSE(staleResult.available);
    EXPECT_TRUE(staleResult.errorMessage.contains(outputB.outputPath, Qt::CaseSensitive))
        << staleResult.errorMessage.toStdString();
}

TEST_F(TempDirFixture, StarsPlusCometProductResolverFailsClosedWithoutPersistedProductsForExactToken)
{
    an::ui::CometStackingProductResolveRequest request;
    request.stackingMode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
    request.frameIds = {FrameId(QDir(tempPath()).filePath(QStringLiteral("closed_gate_a.fit")))};

    const auto result = an::ui::resolveStarsPlusCometExperimentalProducts(request);
    EXPECT_FALSE(result.available);
    EXPECT_TRUE(result.errorMessage.contains(ImageIntegration::starsPlusCometExperimentalStackingModeToken(),
                                             Qt::CaseInsensitive))
        << result.errorMessage.toStdString();
    EXPECT_TRUE(result.starBaseProducts.empty());
    EXPECT_FALSE(result.cometProducts.available);
}

TEST_F(TempDirFixture, StarsPlusCometProductResolverMatchesAliasStarBaseFrameIdentity)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|stars-plus-comet-products");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("stars_plus_reference.fit")));
    const FrameId frameA(QStringLiteral("C:/Ident/Comet/Stars_Plus_A.fit"));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("stars_plus_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestStarsPlusCometResolverWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto run =
        makeCometRun(QStringLiteral("run-stars-plus-comet"), groupKey, referenceFrame, QStringLiteral("Active"));
    ASSERT_TRUE(writer.upsertGroupState(makeGroupStateForCometRun(run), &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();
    const auto plane = makeCometPlane(run.cometRunId);
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();
    ASSERT_TRUE(
        writer.upsertCometRoleReadiness(makeCometReadiness(run.cometRunId, QStringLiteral("Ready"), true), &error))
        << error.toStdString();

    auto starMetricA = makeReferenceMetric(FrameId(QStringLiteral("c:\\ident\\comet\\stars_plus_a.fit")), groupKey,
                                           run.settingsVersion, 122, 2.3, 29.0);
    starMetricA.outputPath = QDir(tempPath()).filePath(QStringLiteral("star_aligned/stars_plus_a.fit"));
    writePlaceholderFile(starMetricA.outputPath);
    ASSERT_TRUE(writer.upsertMetric(starMetricA, &error)) << error.toStdString();

    auto starMetricB = makeReferenceMetric(frameB, groupKey, run.settingsVersion, 118, 2.5, 27.0);
    starMetricB.outputPath = QDir(tempPath()).filePath(QStringLiteral("star_aligned/stars_plus_b.fit"));
    writePlaceholderFile(starMetricB.outputPath);
    ASSERT_TRUE(writer.upsertMetric(starMetricB, &error)) << error.toStdString();

    const auto cometOutputA = makeCometRegisteredOutput(
        run, plane, frameA, QDir(tempPath()).filePath(QStringLiteral("comet_aligned/stars_plus_a.fit")),
        QStringLiteral("stars-plus-comet-a"));
    writePlaceholderFile(cometOutputA.outputPath);
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(cometOutputA, &error)) << error.toStdString();

    const auto cometOutputB = makeCometRegisteredOutput(
        run, plane, frameB, QDir(tempPath()).filePath(QStringLiteral("comet_aligned/stars_plus_b.fit")),
        QStringLiteral("stars-plus-comet-b"));
    writePlaceholderFile(cometOutputB.outputPath);
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(cometOutputB, &error)) << error.toStdString();

    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = dbPath;
    request.groupKey = groupKey;
    request.registrationHash = run.registrationHash;
    request.lightFilesHash = run.lightFilesHash;
    request.calibrationHash = run.calibrationHash;
    request.resolvedReferenceFrameId = run.resolvedReferenceFrameId;
    request.stackingMode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
    request.drizzle = false;
    request.frameIds = {frameA, frameB};

    const auto result = an::ui::resolveStarsPlusCometExperimentalProducts(request);
    ASSERT_TRUE(result.available) << result.errorMessage.toStdString();
    EXPECT_EQ(result.cometProducts.kind, an::ui::CometStackingProductKind::RegisteredOutputs);
    EXPECT_EQ(result.cometProducts.cometRunId, run.cometRunId);
    ASSERT_EQ(result.starBaseProducts.size(), 2u);
    EXPECT_EQ(result.starBaseProducts[0].outputPath, starMetricA.outputPath);
    EXPECT_EQ(result.starBaseProducts[1].outputPath, starMetricB.outputPath);

    QFile replacement(starMetricB.outputPath);
    ASSERT_TRUE(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(replacement.write("replacement-star-base-generation"), qint64(32));
    replacement.close();

    const auto staleResult = an::ui::resolveStarsPlusCometExperimentalProducts(request);
    EXPECT_FALSE(staleResult.available);
    EXPECT_TRUE(staleResult.errorMessage.contains(starMetricB.outputPath, Qt::CaseSensitive))
        << staleResult.errorMessage.toStdString();
}

TEST_F(TempDirFixture, StarsPlusCometProductResolverFailsClosedOnPartialStarBaseProducts)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|stars-plus-comet-partial-star-base");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("partial_star_reference.fit")));
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("partial_star_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("partial_star_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestStarsPlusCometPartialWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto run = makeCometRun(QStringLiteral("run-stars-plus-comet-partial"), groupKey, referenceFrame,
                                  QStringLiteral("Active"));
    ASSERT_TRUE(writer.upsertGroupState(makeGroupStateForCometRun(run), &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();
    const auto plane = makeCometPlane(run.cometRunId);
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();
    ASSERT_TRUE(
        writer.upsertCometRoleReadiness(makeCometReadiness(run.cometRunId, QStringLiteral("Ready"), true), &error))
        << error.toStdString();

    auto starMetricA = makeReferenceMetric(frameA, groupKey, run.settingsVersion, 122, 2.3, 29.0);
    starMetricA.outputPath = QDir(tempPath()).filePath(QStringLiteral("star_aligned/partial_star_a.fit"));
    writePlaceholderFile(starMetricA.outputPath);
    ASSERT_TRUE(writer.upsertMetric(starMetricA, &error)) << error.toStdString();

    const auto cometOutputA = makeCometRegisteredOutput(
        run, plane, frameA, QDir(tempPath()).filePath(QStringLiteral("comet_aligned/partial_star_a.fit")),
        QStringLiteral("partial-comet-a"));
    writePlaceholderFile(cometOutputA.outputPath);
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(cometOutputA, &error)) << error.toStdString();

    const auto cometOutputB = makeCometRegisteredOutput(
        run, plane, frameB, QDir(tempPath()).filePath(QStringLiteral("comet_aligned/partial_star_b.fit")),
        QStringLiteral("partial-comet-b"));
    writePlaceholderFile(cometOutputB.outputPath);
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(cometOutputB, &error)) << error.toStdString();

    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = dbPath;
    request.groupKey = groupKey;
    request.registrationHash = run.registrationHash;
    request.lightFilesHash = run.lightFilesHash;
    request.calibrationHash = run.calibrationHash;
    request.resolvedReferenceFrameId = run.resolvedReferenceFrameId;
    request.stackingMode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
    request.drizzle = false;
    request.frameIds = {frameA, frameB};

    const auto result = an::ui::resolveStarsPlusCometExperimentalProducts(request);
    EXPECT_FALSE(result.available);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("star base"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();
    EXPECT_TRUE(result.errorMessage.contains(frameB.canonicalPath(), Qt::CaseSensitive))
        << result.errorMessage.toStdString();
}

TEST_F(TempDirFixture, StarsPlusCometProductResolverFailsClosedOnStaleStarBaseContext)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|stars-plus-comet-stale-context");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("stale_context_reference.fit")));
    const FrameId frame(QDir(tempPath()).filePath(QStringLiteral("stale_context_a.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestStarsPlusCometStaleWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto run =
        makeCometRun(QStringLiteral("run-stars-plus-comet-stale"), groupKey, referenceFrame, QStringLiteral("Active"));
    auto staleState = makeGroupStateForCometRun(run);
    staleState.registrationHash = QStringLiteral("stale-star-registration-hash");
    ASSERT_TRUE(writer.upsertGroupState(staleState, &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();
    const auto plane = makeCometPlane(run.cometRunId);
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();
    ASSERT_TRUE(
        writer.upsertCometRoleReadiness(makeCometReadiness(run.cometRunId, QStringLiteral("Ready"), true), &error))
        << error.toStdString();

    auto starMetric = makeReferenceMetric(frame, groupKey, run.settingsVersion, 122, 2.3, 29.0);
    starMetric.outputPath = QDir(tempPath()).filePath(QStringLiteral("star_aligned/stale_context_a.fit"));
    writePlaceholderFile(starMetric.outputPath);
    ASSERT_TRUE(writer.upsertMetric(starMetric, &error)) << error.toStdString();

    const auto cometOutput = makeCometRegisteredOutput(
        run, plane, frame, QDir(tempPath()).filePath(QStringLiteral("comet_aligned/stale_context_a.fit")),
        QStringLiteral("stale-comet-a"));
    writePlaceholderFile(cometOutput.outputPath);
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(cometOutput, &error)) << error.toStdString();

    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = dbPath;
    request.groupKey = groupKey;
    request.registrationHash = run.registrationHash;
    request.lightFilesHash = run.lightFilesHash;
    request.calibrationHash = run.calibrationHash;
    request.resolvedReferenceFrameId = run.resolvedReferenceFrameId;
    request.stackingMode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
    request.drizzle = false;
    request.frameIds = {frame};

    const auto result = an::ui::resolveStarsPlusCometExperimentalProducts(request);
    EXPECT_FALSE(result.available);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("star base"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("stale"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();
}

TEST_F(TempDirFixture, CometStackingProductResolverNonDrizzleBlocksWhenCometOutputFileIsMissing)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|stacking-missing-output");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("missing_output_reference.fit")));
    const FrameId frame(QDir(tempPath()).filePath(QStringLiteral("missing_output_light.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometStackMissingOutputWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto run =
        makeCometRun(QStringLiteral("run-stacking-missing-output"), groupKey, referenceFrame, QStringLiteral("Active"));
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();
    const auto plane = makeCometPlane(run.cometRunId);
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();
    ASSERT_TRUE(
        writer.upsertCometRoleReadiness(makeCometReadiness(run.cometRunId, QStringLiteral("Ready"), true), &error))
        << error.toStdString();

    an::ui::CometRegisteredOutputRecord output;
    output.cometRunId = run.cometRunId;
    output.frameId = frame;
    output.role = QStringLiteral("comet_aligned");
    output.cometOutputPlaneId = plane.cometOutputPlaneId;
    output.outputPath = QDir(tempPath()).filePath(QStringLiteral("comet_aligned/missing_output_light.fit"));
    output.artifactHash = QStringLiteral("missing-output-artifact");
    output.succeeded = true;
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(output, &error)) << error.toStdString();

    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = dbPath;
    request.groupKey = groupKey;
    request.registrationHash = run.registrationHash;
    request.lightFilesHash = run.lightFilesHash;
    request.calibrationHash = run.calibrationHash;
    request.resolvedReferenceFrameId = run.resolvedReferenceFrameId;
    request.stackingMode = QStringLiteral("Comet");
    request.drizzle = false;
    request.frameIds = {frame};

    const auto result = an::ui::resolveCometStackingProducts(request);
    EXPECT_FALSE(result.available);
    EXPECT_EQ(result.kind, an::ui::CometStackingProductKind::None);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("missing comet_aligned registered output file"),
                                             Qt::CaseInsensitive))
        << result.errorMessage.toStdString();
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("alignment"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();
}

TEST_F(TempDirFixture, CometStackingProductResolverDrizzleUsesCometTransformsWithoutMaterializedOutputs)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|stacking-drizzle-transforms");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("drizzle_reference.fit")));
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("drizzle_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("drizzle_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometStackDrizzleWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto run =
        makeCometRun(QStringLiteral("run-stacking-drizzle"), groupKey, referenceFrame, QStringLiteral("Active"));
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();
    const auto plane = makeCometPlane(run.cometRunId);
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometRoleReadiness(
        makeCometReadiness(run.cometRunId, QStringLiteral("TransformReady"), false), &error))
        << error.toStdString();

    an::ui::CometTransformRoleRecord transformA;
    transformA.cometRunId = run.cometRunId;
    transformA.frameId = frameA;
    transformA.role = QStringLiteral("comet_aligned");
    transformA.cometOutputPlaneId = plane.cometOutputPlaneId;
    transformA.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(1.0, 2.0));
    transformA.succeeded = true;
    ASSERT_TRUE(writer.upsertCometTransformRole(transformA, &error)) << error.toStdString();

    an::ui::CometTransformRoleRecord transformB = transformA;
    transformB.frameId = frameB;
    transformB.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(-3.0, 4.0));
    ASSERT_TRUE(writer.upsertCometTransformRole(transformB, &error)) << error.toStdString();

    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = dbPath;
    request.groupKey = groupKey;
    request.registrationHash = run.registrationHash;
    request.lightFilesHash = run.lightFilesHash;
    request.calibrationHash = run.calibrationHash;
    request.resolvedReferenceFrameId = run.resolvedReferenceFrameId;
    request.stackingMode = QStringLiteral("Comet");
    request.drizzle = true;
    request.frameIds = {frameA, frameB};

    const auto result = an::ui::resolveCometStackingProducts(request);
    ASSERT_TRUE(result.available) << result.errorMessage.toStdString();
    EXPECT_EQ(result.kind, an::ui::CometStackingProductKind::TransformRoles);
    EXPECT_EQ(result.cometRunId, run.cometRunId);
    ASSERT_EQ(result.transforms.size(), 2U);
    EXPECT_EQ(result.transforms[0].frameId, frameA);
    EXPECT_EQ(result.transforms[1].frameId, frameB);
    EXPECT_TRUE(result.registeredOutputs.empty());
}

TEST_F(TempDirFixture, CometRunActivationRejectsFailedOrCancelledCandidates)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|lifecycle-reject");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("reference.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometLifecycleRejectWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto active = makeCometRun(QStringLiteral("run-active"), groupKey, referenceFrame, QStringLiteral("Active"));
    ASSERT_TRUE(writer.upsertCometTrackingRun(active, &error)) << error.toStdString();

    for (const auto& status : {QStringLiteral("Failed"), QStringLiteral("Cancelled")})
    {
        auto candidate = makeCometRun(QStringLiteral("run-%1").arg(status.toLower()), groupKey, referenceFrame, status);
        ASSERT_TRUE(writer.upsertCometTrackingRun(candidate, &error)) << error.toStdString();

        error.clear();
        EXPECT_FALSE(
            writer.activateCometTrackingRun(candidate.cometRunId, {QStringLiteral("comet_aligned")}, false, &error));
        EXPECT_TRUE(error.contains(status, Qt::CaseInsensitive)) << error.toStdString();
    }

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCometLifecycleRejectLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    const auto activeRun =
        loader.loadActiveCometRunForContext(groupKey, active.registrationHash, active.lightFilesHash,
                                            active.calibrationHash, active.resolvedReferenceFrameId, &error);
    ASSERT_TRUE(activeRun.has_value()) << error.toStdString();
    EXPECT_EQ(activeRun->cometRunId, active.cometRunId);
}

TEST_F(TempDirFixture, CometRunActivationSupersedesPriorActiveRunAtomically)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|lifecycle-activate");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("reference.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometLifecycleActivateWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto active = makeCometRun(QStringLiteral("run-active"), groupKey, referenceFrame, QStringLiteral("Active"));
    ASSERT_TRUE(writer.upsertCometTrackingRun(active, &error)) << error.toStdString();

    const auto candidate = makeCometRun(QStringLiteral("run-registration-ready"), groupKey, referenceFrame,
                                        QStringLiteral("RegistrationReady"));
    ASSERT_TRUE(writer.upsertCometTrackingRun(candidate, &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometOutputPlane(makeCometPlane(candidate.cometRunId), &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometRoleReadiness(
        makeCometReadiness(candidate.cometRunId, QStringLiteral("TransformReady"), false), &error))
        << error.toStdString();

    ASSERT_TRUE(writer.activateCometTrackingRun(candidate.cometRunId, {QStringLiteral("comet_aligned")}, false, &error))
        << error.toStdString();

    EXPECT_EQ(loadCometRunStatus(dbPath, active.cometRunId), QStringLiteral("Superseded"));
    EXPECT_EQ(loadCometRunStatus(dbPath, candidate.cometRunId), QStringLiteral("Active"));

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCometLifecycleActivateLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    const auto activeRun =
        loader.loadActiveCometRunForContext(groupKey, candidate.registrationHash, candidate.lightFilesHash,
                                            candidate.calibrationHash, candidate.resolvedReferenceFrameId, &error);
    ASSERT_TRUE(activeRun.has_value()) << error.toStdString();
    EXPECT_EQ(activeRun->cometRunId, candidate.cometRunId);
}

TEST_F(TempDirFixture, CometRunActivationAllowsTransformReadyRunToReceiveOutputsLater)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|lifecycle-transform-ready");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("reference.fit")));
    const FrameId lightFrame(QDir(tempPath()).filePath(QStringLiteral("light.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometLifecycleTransformReadyWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto run = makeCometRun(QStringLiteral("run-transform-ready"), groupKey, referenceFrame,
                                  QStringLiteral("RegistrationReady"));
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();
    const auto plane = makeCometPlane(run.cometRunId);
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometRoleReadiness(
        makeCometReadiness(run.cometRunId, QStringLiteral("TransformReady"), false), &error))
        << error.toStdString();

    ASSERT_TRUE(writer.activateCometTrackingRun(run.cometRunId, {QStringLiteral("comet_aligned")}, false, &error))
        << error.toStdString();

    an::ui::CometRegisteredOutputRecord output;
    output.cometRunId = run.cometRunId;
    output.frameId = lightFrame;
    output.role = QStringLiteral("comet_aligned");
    output.cometOutputPlaneId = plane.cometOutputPlaneId;
    output.outputPath = QDir(tempPath()).filePath(QStringLiteral("registered/light.fit"));
    output.artifactHash = QStringLiteral("artifact-lifecycle");
    output.succeeded = true;
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(output, &error)) << error.toStdString();
    ASSERT_TRUE(
        writer.upsertCometRoleReadiness(makeCometReadiness(run.cometRunId, QStringLiteral("Ready"), true), &error))
        << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCometLifecycleTransformReadyLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    const auto activeRun = loader.loadActiveCometRunForContext(
        groupKey, run.registrationHash, run.lightFilesHash, run.calibrationHash, run.resolvedReferenceFrameId, &error);
    ASSERT_TRUE(activeRun.has_value()) << error.toStdString();
    EXPECT_EQ(activeRun->cometRunId, run.cometRunId);
    EXPECT_EQ(loadCometRunStatus(dbPath, run.cometRunId), QStringLiteral("Active"));

    const auto loadedOutput =
        loader.loadCometRegisteredOutput(run.cometRunId, lightFrame, QStringLiteral("comet_aligned"), &error);
    ASSERT_TRUE(loadedOutput.has_value()) << error.toStdString();
    EXPECT_EQ(loadedOutput->artifactHash, output.artifactHash);
}

TEST_F(TempDirFixture, CometRunActivationRequiresMaterializedOutputsWhenRequested)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|lifecycle-materialized");
    const FrameId referenceFrame(QDir(tempPath()).filePath(QStringLiteral("reference.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometLifecycleMaterializedWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto run =
        makeCometRun(QStringLiteral("run-materialized"), groupKey, referenceFrame, QStringLiteral("RegistrationReady"));
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometOutputPlane(makeCometPlane(run.cometRunId), &error)) << error.toStdString();
    ASSERT_TRUE(writer.upsertCometRoleReadiness(
        makeCometReadiness(run.cometRunId, QStringLiteral("TransformReady"), false), &error))
        << error.toStdString();

    error.clear();
    EXPECT_FALSE(writer.activateCometTrackingRun(run.cometRunId, {QStringLiteral("comet_aligned")}, true, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("materialized outputs"), Qt::CaseInsensitive)) << error.toStdString();
    EXPECT_EQ(loadCometRunStatus(dbPath, run.cometRunId), QStringLiteral("RegistrationReady"));

    ASSERT_TRUE(
        writer.upsertCometRoleReadiness(makeCometReadiness(run.cometRunId, QStringLiteral("Ready"), true), &error))
        << error.toStdString();
    ASSERT_TRUE(writer.activateCometTrackingRun(run.cometRunId, {QStringLiteral("comet_aligned")}, true, &error))
        << error.toStdString();
    EXPECT_EQ(loadCometRunStatus(dbPath, run.cometRunId), QStringLiteral("Active"));
}

TEST_F(TempDirFixture, CometSidecarExplicitProductReadersReportMissingProducts)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|comet|missing-products");
    const FrameId frame(QDir(tempPath()).filePath(QStringLiteral("missing_product.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometMissingProductWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::CometTrackingRunRecord run;
    run.cometRunId = QStringLiteral("comet-run-missing-products");
    run.groupKey = groupKey;
    run.settingsVersion = 1;
    run.registrationHash = QStringLiteral("reg-missing-products");
    run.lightFilesHash = QStringLiteral("lights-missing-products");
    run.calibrationHash = QStringLiteral("cal-missing-products");
    run.resolvedReferenceFrameId = frame.canonicalPath();
    run.mode = QStringLiteral("PureComet");
    run.status = QStringLiteral("Active");
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCometMissingProductLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    error.clear();
    EXPECT_FALSE(loader.loadCometRoleReadiness(run.cometRunId, QStringLiteral("comet_aligned"), &error).has_value());
    EXPECT_TRUE(error.contains(QStringLiteral("readiness"), Qt::CaseInsensitive)) << error.toStdString();

    error.clear();
    EXPECT_FALSE(loader.loadCometOutputPlane(QStringLiteral("missing-plane"), &error).has_value());
    EXPECT_TRUE(error.contains(QStringLiteral("output plane"), Qt::CaseInsensitive)) << error.toStdString();

    error.clear();
    EXPECT_FALSE(
        loader.loadCometTransformRole(run.cometRunId, frame, QStringLiteral("comet_aligned"), &error).has_value());
    EXPECT_TRUE(error.contains(QStringLiteral("transform"), Qt::CaseInsensitive)) << error.toStdString();

    error.clear();
    EXPECT_FALSE(
        loader.loadCometRegisteredOutput(run.cometRunId, frame, QStringLiteral("comet_aligned"), &error).has_value());
    EXPECT_TRUE(error.contains(QStringLiteral("registered output"), Qt::CaseInsensitive)) << error.toStdString();
}

TEST_F(TempDirFixture, RegistrationMetricsWriterDeleteAllForGroupClearsCometSidecarsOnlyForTargetGroup)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupA = QStringLiteral("reg|comet|deleteA");
    const QString groupB = QStringLiteral("reg|comet|deleteB");
    const FrameId frameA(QDir(tempPath()).filePath(QStringLiteral("delete_a.fit")));
    const FrameId frameB(QDir(tempPath()).filePath(QStringLiteral("delete_b.fit")));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("TestCometDeleteWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    const auto insertRun = [&](const QString& groupKey, const QString& runId, const FrameId& frameId) {
        an::ui::CometTrackingRunRecord run;
        run.cometRunId = runId;
        run.groupKey = groupKey;
        run.settingsVersion = 3;
        run.registrationHash = QStringLiteral("reg-%1").arg(runId);
        run.lightFilesHash = QStringLiteral("lights-%1").arg(runId);
        run.calibrationHash = QStringLiteral("cal-%1").arg(runId);
        run.resolvedReferenceFrameId = frameId.canonicalPath();
        run.mode = QStringLiteral("PureComet");
        run.status = QStringLiteral("Active");
        ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();

        an::ui::CometTrackingRunSummaryRecord summary;
        summary.cometRunId = runId;
        summary.fitModel = QStringLiteral("linear");
        summary.acceptedInputCount = 2;
        summary.updatedAt = 1700000700;
        ASSERT_TRUE(writer.upsertCometTrackingRunSummary(summary, &error)) << error.toStdString();

        an::ui::CometTrackingPointRecord point;
        point.pointId = QStringLiteral("point-%1").arg(runId);
        point.cometRunId = runId;
        point.originalFrameId = frameId;
        point.origin = QStringLiteral("Manual");
        point.fitState = QStringLiteral("Inlier");
        point.measurementMethod = QStringLiteral("manual");
        point.updatedAt = 1700000701;
        ASSERT_TRUE(writer.upsertCometTrackingPoint(point, &error)) << error.toStdString();

        an::ui::CometOutputPlaneRecord plane;
        plane.cometOutputPlaneId = QStringLiteral("plane-%1").arg(runId);
        plane.cometRunId = runId;
        plane.role = QStringLiteral("comet_aligned");
        plane.alignmentMode = QStringLiteral("PureComet");
        plane.coordinateFrameKind = QStringLiteral("CometReference");
        plane.canvasWidth = 128;
        plane.canvasHeight = 128;
        plane.skyWcsValid = false;
        plane.skyWcsInvalidReason = QStringLiteral("non-sidereal");
        ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();

        an::ui::CometTransformRoleRecord transform;
        transform.cometRunId = runId;
        transform.frameId = frameId;
        transform.role = QStringLiteral("comet_aligned");
        transform.cometOutputPlaneId = plane.cometOutputPlaneId;
        transform.transformJson = stage_utils::serializeTransformJson(makeTranslationTransform(0.5, 1.5));
        transform.succeeded = true;
        ASSERT_TRUE(writer.upsertCometTransformRole(transform, &error)) << error.toStdString();

        an::ui::CometRegisteredOutputRecord output;
        output.cometRunId = runId;
        output.frameId = frameId;
        output.role = QStringLiteral("comet_aligned");
        output.cometOutputPlaneId = plane.cometOutputPlaneId;
        output.outputPath = QDir(tempPath()).filePath(QStringLiteral("%1-output.fit").arg(runId));
        output.artifactHash = QStringLiteral("artifact-%1").arg(runId);
        output.succeeded = true;
        ASSERT_TRUE(writer.upsertCometRegisteredOutput(output, &error)) << error.toStdString();

        an::ui::CometRoleReadinessRecord readiness;
        readiness.cometRunId = runId;
        readiness.role = QStringLiteral("comet_aligned");
        readiness.readinessStatus = QStringLiteral("Ready");
        readiness.outputPlaneStatus = QStringLiteral("Ready");
        ASSERT_TRUE(writer.upsertCometRoleReadiness(readiness, &error)) << error.toStdString();

        ASSERT_TRUE(writer.upsertStackingCometArtifact(
            makeStackingCometArtifact(runId, plane.cometOutputPlaneId, QStringLiteral("FinalComposite"),
                                      QDir(tempPath()).filePath(QStringLiteral("%1-final.fit").arg(runId)),
                                      QStringLiteral("stacking-%1").arg(runId)),
            &error))
            << error.toStdString();
    };

    insertRun(groupA, QStringLiteral("run-delete-a"), frameA);
    insertRun(groupB, QStringLiteral("run-delete-b"), frameB);

    ASSERT_TRUE(writer.deleteAllForGroup(groupA, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("TestCometDeleteLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();

    EXPECT_FALSE(loader.loadCometOutputPlane(QStringLiteral("plane-run-delete-a"), &error).has_value())
        << error.toStdString();
    EXPECT_FALSE(loader.loadCometTrackingRunSummary(QStringLiteral("run-delete-a"), &error).has_value())
        << error.toStdString();
    std::vector<an::ui::CometTrackingPointRecord> pointsA;
    ASSERT_TRUE(loader.loadCometTrackingPoints(QStringLiteral("run-delete-a"), pointsA, &error)) << error.toStdString();
    EXPECT_TRUE(pointsA.empty());
    EXPECT_FALSE(
        loader.loadCometTransformRole(QStringLiteral("run-delete-a"), frameA, QStringLiteral("comet_aligned"), &error)
            .has_value())
        << error.toStdString();
    EXPECT_FALSE(
        loader
            .loadCometRegisteredOutput(QStringLiteral("run-delete-a"), frameA, QStringLiteral("comet_aligned"), &error)
            .has_value())
        << error.toStdString();
    EXPECT_FALSE(loader.loadCometRoleReadiness(QStringLiteral("run-delete-a"), QStringLiteral("comet_aligned"), &error)
                     .has_value())
        << error.toStdString();
    std::vector<an::ui::StackingCometArtifactRecord> artifactsA;
    ASSERT_TRUE(loader.loadStackingCometArtifacts(QStringLiteral("run-delete-a"), artifactsA, &error))
        << error.toStdString();
    EXPECT_TRUE(artifactsA.empty());
    EXPECT_FALSE(loader
                     .loadActiveCometRunForContext(groupA, QStringLiteral("reg-run-delete-a"),
                                                   QStringLiteral("lights-run-delete-a"),
                                                   QStringLiteral("cal-run-delete-a"), frameA.canonicalPath(), &error)
                     .has_value())
        << error.toStdString();

    EXPECT_TRUE(loader.loadCometOutputPlane(QStringLiteral("plane-run-delete-b"), &error).has_value())
        << error.toStdString();
    EXPECT_TRUE(loader.loadCometTrackingRunSummary(QStringLiteral("run-delete-b"), &error).has_value())
        << error.toStdString();
    std::vector<an::ui::CometTrackingPointRecord> pointsB;
    ASSERT_TRUE(loader.loadCometTrackingPoints(QStringLiteral("run-delete-b"), pointsB, &error)) << error.toStdString();
    ASSERT_EQ(pointsB.size(), 1u);
    EXPECT_TRUE(
        loader.loadCometTransformRole(QStringLiteral("run-delete-b"), frameB, QStringLiteral("comet_aligned"), &error)
            .has_value())
        << error.toStdString();
    EXPECT_TRUE(
        loader
            .loadCometRegisteredOutput(QStringLiteral("run-delete-b"), frameB, QStringLiteral("comet_aligned"), &error)
            .has_value())
        << error.toStdString();
    EXPECT_TRUE(loader.loadCometRoleReadiness(QStringLiteral("run-delete-b"), QStringLiteral("comet_aligned"), &error)
                    .has_value())
        << error.toStdString();
    std::vector<an::ui::StackingCometArtifactRecord> artifactsB;
    ASSERT_TRUE(loader.loadStackingCometArtifacts(QStringLiteral("run-delete-b"), artifactsB, &error))
        << error.toStdString();
    ASSERT_EQ(artifactsB.size(), 1u);
    EXPECT_TRUE(loader
                    .loadActiveCometRunForContext(groupB, QStringLiteral("reg-run-delete-b"),
                                                  QStringLiteral("lights-run-delete-b"),
                                                  QStringLiteral("cal-run-delete-b"), frameB.canonicalPath(), &error)
                    .has_value())
        << error.toStdString();
}

TEST(RegistrationSettingsPersistenceTest, JsonRoundTripCoversEveryParameter)
{
    RegistrationSettings settings;
    settings.version = 7;
    settings.minStarPairs = 21;
    settings.maxStarPairs = 987;
    settings.autoReferenceDuringRegistration = false;
    settings.autoSolveUnsolvedFrames = true;
    settings.enrichWithGaiaCatalog = false;
    settings.referenceImage = QStringLiteral("C:/sessions/reference.fit");
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.transformation = QStringLiteral("Homography");
    settings.transformMode = QStringLiteral("polynomial");
    settings.transformOrder = 3;
    settings.twoPassRefinementEnabled = true;
    settings.alignmentMode = QStringLiteral("mosaic");
    settings.interpolation = QStringLiteral("Bicubic");

    RegistrationSettings::ManualControlPoint point;
    point.sourcePoint = QPointF(12.5, 19.25);
    point.referencePoint = QPointF(15.0, 23.5);
    settings.manualControlPointsByFrame.insert(QStringLiteral("C:/sessions/light-01.fit"), {point});

    RegistrationSettings::CometTrackingSettings comet;
    RegistrationSettings::CometTrackingObservation observation;
    observation.frameId = QStringLiteral("C:/sessions/light-01.fit");
    observation.sourcePosition = QPointF(320.25, 180.75);
    observation.origin = RegistrationSettings::CometObservationOrigin::AcceptedAutomatic;
    observation.enabled = true;
    observation.apertureRadiusPixels = 7.5;
    observation.note = QStringLiteral("accepted");
    comet.observations.push_back(observation);
    comet.trackingOptions.searchRadiusPixels = 48.0;
    comet.trackingOptions.minimumSnr = 4.5;
    comet.trackingOptions.maxBootstrapFrames = 12;
    comet.fitOptions.model = QStringLiteral("quadratic");
    comet.fitOptions.robustRejection = false;
    comet.fitOptions.outlierSigma = 2.5;
    comet.outputMode = QStringLiteral("Comet + Stars");
    settings.cometTracking = comet;

    settings.chromaticAberration.enabled = true;
    settings.chromaticAberration.edgeFraction = 0.62;
    settings.chromaticAberration.transformType = QStringLiteral("Affine");
    settings.chromaticAberration.minMatchedStars = 12;
    settings.chromaticAberration.interpolation = QStringLiteral("Bicubic");
    settings.radius = 9;
    settings.threshold = 2.25;
    settings.amplitudeRangeEnabled = true;
    settings.thresholdMinimum = 0.1;
    settings.thresholdMaximum = 0.95;
    settings.roundnessThreshold = 0.35;
    settings.roundnessRangeEnabled = true;
    settings.roundnessThresholdMaximum = 0.88;
    settings.convergence = 4;
    settings.relaxPsfChecks = true;
    settings.profileType = QStringLiteral("Moffat");
    settings.minimumBeta = 2.2;
    settings.cfaPattern = QStringLiteral("RGGB");
    settings.debayerMethod = QStringLiteral("RCD");

    const QJsonObject serialized = ImageIntegration::RegistrationHashUtils::registrationSettingsToJson(settings);
    const QStringList expected_keys = {
        QStringLiteral("version"),
        QStringLiteral("minStarPairs"),
        QStringLiteral("maxStarPairs"),
        QStringLiteral("autoReferenceDuringRegistration"),
        QStringLiteral("autoSolveUnsolvedFrames"),
        QStringLiteral("enrichWithGaiaCatalog"),
        QStringLiteral("referenceImage"),
        QStringLiteral("registrationMethod"),
        QStringLiteral("transformation"),
        QStringLiteral("transformMode"),
        QStringLiteral("transformOrder"),
        QStringLiteral("twoPassRefinementEnabled"),
        QStringLiteral("alignmentMode"),
        QStringLiteral("interpolation"),
        QStringLiteral("manualControlPoints"),
        QStringLiteral("cometTracking"),
        QStringLiteral("chromaticAberration"),
        QStringLiteral("radius"),
        QStringLiteral("threshold"),
        QStringLiteral("amplitudeRangeEnabled"),
        QStringLiteral("thresholdMinimum"),
        QStringLiteral("thresholdMaximum"),
        QStringLiteral("roundnessThreshold"),
        QStringLiteral("roundnessRangeEnabled"),
        QStringLiteral("roundnessThresholdMaximum"),
        QStringLiteral("convergence"),
        QStringLiteral("relaxPsfChecks"),
        QStringLiteral("profileType"),
        QStringLiteral("minimumBeta"),
        QStringLiteral("cfaPattern"),
        QStringLiteral("debayerMethod"),
    };
    for (const auto& key : expected_keys)
    {
        EXPECT_TRUE(serialized.contains(key)) << key.toStdString();
    }

    const RegistrationSettings restored =
        ImageIntegration::RegistrationHashUtils::registrationSettingsFromJson(serialized);
    EXPECT_EQ(ImageIntegration::RegistrationHashUtils::registrationSettingsToJson(restored), serialized);
}

// ---------------------------------------------------------------------------
// Frame noise statistics
//
// Background noise and the normalization scale estimators are the only registration metrics that
// respond to transparency, and they are the sole input to noise weighting. They are measured during
// star detection and are not recoverable from the star blob (which carries per-star geometry only),
// so both the metric rows and the detection cache have to carry them: a cache hit that skipped
// detection would otherwise write zeros back over good values.
// ---------------------------------------------------------------------------

TEST_F(TempDirFixture, RegistrationMetricRoundTripPreservesFrameNoiseStatistics)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|noise|metrics");
    constexpr int kSettingsVersion = 7;
    const FrameId frame(QStringLiteral("C:/Noise/Metrics/light.fit"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("NoiseMetricWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    auto metric = makeReferenceMetric(frame, groupKey, kSettingsVersion, 40, 2.5, 30.0);
    metric.noise.background_noise = 0.0031;
    metric.noise.global_mad = 0.0042;
    metric.noise.global_scale = 0.0055;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("NoiseMetricLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMetricRecord> loaded;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, kSettingsVersion, loaded, &error)) << error.toStdString();

    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_DOUBLE_EQ(loaded.front().noise.background_noise, metric.noise.background_noise);
    EXPECT_DOUBLE_EQ(loaded.front().noise.global_mad, metric.noise.global_mad);
    EXPECT_DOUBLE_EQ(loaded.front().noise.global_scale, metric.noise.global_scale);
}

TEST_F(TempDirFixture, RegistrationStarCacheRoundTripPreservesFrameNoiseStatistics)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|noise|star-cache");
    const QString detectionHash = QStringLiteral("detect-noise-hash");
    const FrameId frame(QStringLiteral("C:/Noise/StarCache/light.fit"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("NoiseStarCacheWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationStarCacheEntry entry;
    entry.frameId = frame;
    entry.groupKey = groupKey;
    entry.detectionHash = detectionHash;
    entry.inputPath = frame.canonicalPath();
    entry.inputMtime = 1700000123;
    entry.starsCount = 3;
    entry.fwhm = 2.4;
    entry.snr = 25.0;
    entry.roundness = 0.9;
    entry.noise.background_noise = 0.0017;
    entry.noise.global_mad = 0.0023;
    entry.noise.global_scale = 0.0029;
    entry.starsBlob = an::ui::serializeRegistrationStarCacheBlob(makeStars({QPointF(3.0, 4.0)}));
    ASSERT_TRUE(writer.upsertStarCache(entry, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("NoiseStarCacheLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::unordered_map<QString, an::ui::RegistrationStarCacheEntry> cache;
    ASSERT_TRUE(loader.loadStarCacheForGroup(groupKey, detectionHash, cache, &error)) << error.toStdString();

    ASSERT_EQ(cache.size(), 1u);
    const auto& loaded = cache.begin()->second;
    EXPECT_DOUBLE_EQ(loaded.noise.background_noise, entry.noise.background_noise);
    EXPECT_DOUBLE_EQ(loaded.noise.global_mad, entry.noise.global_mad);
    EXPECT_DOUBLE_EQ(loaded.noise.global_scale, entry.noise.global_scale);
}

TEST_F(TempDirFixture, RegistrationMetricsSettingsVersionCopyCarriesFrameNoiseStatistics)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|noise|version-copy");
    constexpr int kFromVersion = 3;
    constexpr int kToVersion = 4;
    const FrameId frame(QStringLiteral("C:/Noise/VersionCopy/light.fit"));

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("NoiseVersionCopyWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    auto metric = makeReferenceMetric(frame, groupKey, kFromVersion, 40, 2.5, 30.0);
    metric.noise.background_noise = 0.0011;
    metric.noise.global_mad = 0.0013;
    metric.noise.global_scale = 0.0017;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    ASSERT_TRUE(writer.copyMetricsToSettingsVersion(groupKey, kFromVersion, kToVersion, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("NoiseVersionCopyLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    std::vector<an::ui::RegistrationMetricRecord> loaded;
    ASSERT_TRUE(loader.loadMetricRecords(groupKey, kToVersion, loaded, &error)) << error.toStdString();

    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_DOUBLE_EQ(loaded.front().noise.background_noise, metric.noise.background_noise);
    EXPECT_DOUBLE_EQ(loaded.front().noise.global_mad, metric.noise.global_mad);
    EXPECT_DOUBLE_EQ(loaded.front().noise.global_scale, metric.noise.global_scale);
}

TEST_F(TempDirFixture, RestoredRegisteredDataCarriesFrameNoiseStatisticsFromBothSources)
{
    const QString dbPath = metricsDbPath(tempPath());
    const QString groupKey = QStringLiteral("reg|noise|restore");
    const QString detectionHash = QStringLiteral("detect-noise-restore");
    constexpr int kSettingsVersion = 5;

    const QString cacheOnlyInput = QDir(tempPath()).filePath(QStringLiteral("cache_only.fit"));
    const QString metricInput = QDir(tempPath()).filePath(QStringLiteral("with_metric.fit"));
    writePlaceholderFile(cacheOnlyInput);
    writePlaceholderFile(metricInput);
    const FrameId cacheOnlyFrame(cacheOnlyInput);
    const FrameId metricFrame(metricInput);

    an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("NoiseRestoreWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationGroupState groupState;
    groupState.groupKey = groupKey;
    groupState.settingsVersion = kSettingsVersion;
    groupState.detectionHash = detectionHash;
    groupState.registrationHash = QStringLiteral("registration-noise-restore");
    groupState.referenceFrameId = metricFrame.canonicalPath();
    groupState.lightFilesHash = QStringLiteral("lights-noise-restore");
    ASSERT_TRUE(writer.upsertGroupState(groupState, &error)) << error.toStdString();

    // Detection ran for both frames, so the star cache carries the noise statistics...
    const auto makeCache = [&](const FrameId& frame, const QString& input, double noise, double mad, double scale) {
        an::ui::RegistrationStarCacheEntry entry;
        entry.frameId = frame;
        entry.groupKey = groupKey;
        entry.detectionHash = detectionHash;
        entry.inputPath = input;
        entry.inputMtime = 1700000200;
        entry.starsCount = 2;
        entry.fwhm = 2.2;
        entry.snr = 22.0;
        entry.roundness = 0.9;
        entry.noise.background_noise = noise;
        entry.noise.global_mad = mad;
        entry.noise.global_scale = scale;
        entry.starsBlob = an::ui::serializeRegistrationStarCacheBlob(makeStars({QPointF(4.0, 5.0)}));
        return entry;
    };
    ASSERT_TRUE(writer.upsertStarCache(makeCache(cacheOnlyFrame, cacheOnlyInput, 0.0031, 0.0041, 0.0051), &error))
        << error.toStdString();
    ASSERT_TRUE(writer.upsertStarCache(makeCache(metricFrame, metricInput, 0.0009, 0.0009, 0.0009), &error))
        << error.toStdString();

    // ...and the metric row, which is written per settings version, wins where it has a value.
    auto metric = makeReferenceMetric(metricFrame, groupKey, kSettingsVersion, 20, 2.2, 22.0);
    metric.noise.background_noise = 0.0072;
    metric.noise.global_mad = 0.0082;
    metric.noise.global_scale = 0.0092;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(dbPath, QStringLiteral("NoiseRestoreLoader"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    const auto state = loader.loadState(groupKey, kSettingsVersion, {}, &error);
    ASSERT_TRUE(state.has_value()) << error.toStdString();
    const RegisteredFrameSet& registered = state->registeredFrames;

    const auto cacheOnlyIt = registered.frames.find(cacheOnlyInput);
    ASSERT_NE(cacheOnlyIt, registered.frames.end());
    EXPECT_DOUBLE_EQ(cacheOnlyIt->second.noise.background_noise, 0.0031);
    EXPECT_DOUBLE_EQ(cacheOnlyIt->second.noise.global_mad, 0.0041);
    EXPECT_DOUBLE_EQ(cacheOnlyIt->second.noise.global_scale, 0.0051);

    const auto metricIt = registered.frames.find(metricInput);
    ASSERT_NE(metricIt, registered.frames.end());
    EXPECT_DOUBLE_EQ(metricIt->second.noise.background_noise, metric.noise.background_noise);
    EXPECT_DOUBLE_EQ(metricIt->second.noise.global_mad, metric.noise.global_mad);
    EXPECT_DOUBLE_EQ(metricIt->second.noise.global_scale, metric.noise.global_scale);
}
