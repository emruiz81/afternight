// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What must hold of every document the integration reads back from disk.
///
/// A project's session, its presets and every artifact a run leaves beside its images are files a person can copy,
/// edit, truncate or keep from another version, and the integration reads each of them back to decide what to reuse:
/// the session document, the envelope every non-image document is published in, a registration's transform records, a
/// drizzle descriptor, a normalization set's coefficients, and the report model a run's archive keeps, which a later
/// run compares itself with. Each is a parser whose input is arbitrary.
///
/// The claims:
///
/// * **A reader answers.** Each reader returns what it read or refuses with a reason; none may crash, and none may
///   succeed without the value it promised.
/// * **What a reader accepted, the writer writes, and the writing reads back the same.** A session document read
///   writes back as a format 3 document that reads again; an envelope decoded encodes into one that decodes to the
///   same fields; a transform record read writes back as one that reads and writes the same; a drizzle descriptor or a
///   coefficient set read and formatted formats the same after it is read again; a stored report read writes back as
///   a document that reads and writes the same. A writer whose output its own reader refuses is the defect this
///   exists to find, because the next run would read that as a corrupt artifact and recompute it every time - or, for
///   a stored report, list the run as one that keeps none.

#include "../FuzzEngine/FuzzHarness.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/SessionDocument.h"
#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"
#include "processes/ImageIntegration/reporting/ReportModelJson.h"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Core/Result.h>

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTime>
#include <QTimeZone>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace an::fuzz
{
namespace
{

/// The modes a seed's first byte selects.
enum Mode : std::uint8_t
{
    Envelope = 0,
    Session = 1,
    TransformRecord = 2,
    DrizzleDescriptor = 3,
    Coefficients = 4,
    StoredReport = 5,
    ModeCount = 6
};

/// The envelope reads ask for: the coefficients' type, any schema up to a few past the current, a payload bound
/// small enough that a mutated length is tried against it.
[[nodiscard]] ImageIntegration::DocumentReadRequest envelopeRequest()
{
    ImageIntegration::DocumentReadRequest request;
    request.documentType = QStringLiteral("normalization-coefficients");
    request.maximumSchemaVersion = 4;
    request.maxPayloadBytes = 1u << 20u;
    return request;
}

[[nodiscard]] std::vector<std::uint8_t> seed(Mode mode, const QByteArray& payload)
{
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(mode)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

void checkEnvelope(const QByteArray& bytes)
{
    const ImageIntegration::DocumentReadResult read =
        ImageIntegration::decodeDocumentEnvelope(bytes, envelopeRequest());
    if (!read.ok())
    {
        AN_FUZZ_CHECK(!read.message.isEmpty(), "an envelope refused without a reason");
        countReached("envelopes-refused");
        return;
    }
    countReached("envelopes-read");
    AN_FUZZ_CHECK(static_cast<std::uint64_t>(read.envelope.payload.size()) <= envelopeRequest().maxPayloadBytes,
                  "an envelope read a payload past the bound it was given");
    const ImageIntegration::DocumentReadResult reread = ImageIntegration::decodeDocumentEnvelope(
        ImageIntegration::encodeDocumentEnvelope(read.envelope), envelopeRequest());
    AN_FUZZ_CHECK(reread.ok(), "an envelope encoded from one that was read cannot be read");
    AN_FUZZ_CHECK(reread.envelope.documentType == read.envelope.documentType &&
                      reread.envelope.schemaVersion == read.envelope.schemaVersion &&
                      reread.envelope.identity == read.envelope.identity &&
                      reread.envelope.payload == read.envelope.payload,
                  "an envelope re-encoded reads as another");
}

void checkSession(const QByteArray& bytes)
{
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes);
    if (!parsed.isObject())
    {
        countReached("sessions-not-objects");
        return;
    }
    const QJsonObject stored = parsed.object();
    AN_FUZZ_CHECK(ImageIntegration::storedSessionSchema(stored) >= -1, "a schema read as something below refusal");
    const ImageIntegration::SessionDocumentResult read =
        ImageIntegration::readSessionDocument(stored, QStringLiteral("fuzz"));
    if (!read.ok())
    {
        AN_FUZZ_CHECK(!read.refusal.isEmpty(), "a session refused without a reason");
        countReached("sessions-refused");
        return;
    }
    countReached("sessions-read");
    const ImageIntegration::SessionDocumentResult written =
        ImageIntegration::writeSessionDocument(*read.document, QStringLiteral("fuzz"));
    if (!written.ok())
    {
        AN_FUZZ_CHECK(!written.refusal.isEmpty(), "a session refused at writing without a reason");
        countReached("sessions-not-rewritable");
        return;
    }
    const ImageIntegration::SessionDocumentResult reread =
        ImageIntegration::readSessionDocument(*written.document, QStringLiteral("fuzz"));
    AN_FUZZ_CHECK(reread.ok(), "a session written from one that was read cannot be read");
    const ImageIntegration::SessionDocumentResult rewritten =
        ImageIntegration::writeSessionDocument(*reread.document, QStringLiteral("fuzz"));
    AN_FUZZ_CHECK(rewritten.ok() && *rewritten.document == *written.document,
                  "a session read back writes otherwise than it did");
    countReached("sessions-round-tripped");
}

void checkTransformRecord(const QByteArray& bytes)
{
    const QString text = QString::fromUtf8(bytes);
    QString error;
    const std::optional<stage_utils::ParsedTransformRecord> parsed = stage_utils::parseTransformRecord(text, error);
    if (!parsed.has_value())
    {
        AN_FUZZ_CHECK(!error.isEmpty(), "a transform record refused without a reason");
        countReached("transforms-refused");
        return;
    }
    countReached("transforms-read");
    const QString written = stage_utils::serializeTransformJson(parsed->transform, parsed->provenance);
    QString rereadError;
    const std::optional<stage_utils::ParsedTransformRecord> reread =
        stage_utils::parseTransformRecord(written, rereadError);
    AN_FUZZ_CHECK(reread.has_value(), "a transform record written again cannot be read");
    AN_FUZZ_CHECK(stage_utils::serializeTransformJson(reread->transform, reread->provenance) == written,
                  "writing a transform record read back is not writing it once");
    countReached("transforms-round-tripped");
}

void checkDrizzleDescriptor(const QByteArray& bytes)
{
    QString error;
    const std::optional<ImageIntegration::DrizzleDescriptorDocument> parsed =
        ImageIntegration::parseDrizzleDescriptor(bytes, error);
    if (!parsed.has_value())
    {
        AN_FUZZ_CHECK(!error.isEmpty(), "a drizzle descriptor refused without a reason");
        countReached("descriptors-refused");
        return;
    }
    countReached("descriptors-read");
    const QByteArray formatted = ImageIntegration::formatDrizzleDescriptor(*parsed);
    QString rereadError;
    const std::optional<ImageIntegration::DrizzleDescriptorDocument> reread =
        ImageIntegration::parseDrizzleDescriptor(formatted, rereadError);
    AN_FUZZ_CHECK(reread.has_value(), "a drizzle descriptor formatted from one that was read cannot be read");
    AN_FUZZ_CHECK(ImageIntegration::formatDrizzleDescriptor(*reread) == formatted,
                  "a drizzle descriptor read back formats otherwise than it did");
    countReached("descriptors-round-tripped");
}

void checkCoefficients(const QByteArray& bytes)
{
    QString error;
    const std::optional<ImageIntegration::NormalizationCoefficientSet> parsed =
        ImageIntegration::parseNormalizationCoefficients(bytes, error);
    if (!parsed.has_value())
    {
        AN_FUZZ_CHECK(!error.isEmpty(), "a coefficient set refused without a reason");
        countReached("coefficients-refused");
        return;
    }
    countReached("coefficients-read");
    AN_FUZZ_CHECK(ImageIntegration::checkNormalizationCoefficientSet(*parsed).isEmpty(),
                  "a coefficient set was read that this build would refuse to write");
    const QByteArray formatted = ImageIntegration::formatNormalizationCoefficients(*parsed);
    AN_FUZZ_CHECK(!formatted.isEmpty(), "a coefficient set that was read cannot be written");
    QString rereadError;
    const std::optional<ImageIntegration::NormalizationCoefficientSet> reread =
        ImageIntegration::parseNormalizationCoefficients(formatted, rereadError);
    AN_FUZZ_CHECK(reread.has_value(), "a coefficient set written from one that was read cannot be read");
    AN_FUZZ_CHECK(ImageIntegration::formatNormalizationCoefficients(*reread) == formatted,
                  "a coefficient set read back writes otherwise than it did");
    countReached("coefficients-round-tripped");
}

void checkStoredReport(const QByteArray& bytes)
{
    namespace Reporting = ImageIntegration::Reporting;
    const Reporting::ReportArchiveRead read = Reporting::ReportArchiveReader::parse(bytes);
    if (!read.isRead())
    {
        AN_FUZZ_CHECK(read.refusal.has_value() && !read.why.isEmpty(), "a stored report refused without a reason");
        countReached("reports-refused");
        return;
    }
    countReached("reports-read");
    AN_FUZZ_CHECK(!read.refusal.has_value() && read.why.isEmpty(), "a stored report was read and refused at once");
    const QJsonObject written = Reporting::ReportModelJson::toJson(*read.model);
    const Reporting::ReportModelRead reread = Reporting::ReportModelJson::fromJson(written);
    AN_FUZZ_CHECK(reread.model.has_value(), "a stored report written from one that was read cannot be read");
    AN_FUZZ_CHECK(Reporting::ReportModelJson::toJson(*reread.model) == written,
                  "a stored report read back writes otherwise than it did");
    countReached("reports-round-tripped");
}

/// A report model of one master with two lights, compared with an earlier run: small enough to be a seed, and with
/// a value in each kind of field the stored document has.
[[nodiscard]] ImageIntegration::Reporting::ReportModel seedReport()
{
    namespace Reporting = ImageIntegration::Reporting;
    Reporting::ReportModel model;
    model.run.outcome = Reporting::ReportOutcome::CompletedWithWarnings;
    model.run.title = QStringLiteral("NGC 253");
    model.run.executionId = QStringLiteral("20260326_071204_118");
    model.run.frames = 2;
    model.run.framesUsed = 1;
    model.run.capturedMs = 360000;
    model.run.integratedMs = 180000;
    model.run.archivedAt = QDateTime(QDate(2026, 3, 26), QTime(10, 12, 4, 118), QTimeZone::UTC);
    model.run.settings.push_back(
        Reporting::ReportSettingsBlock{.title = QStringLiteral("Execution"),
                                       .rows = {{.name = QStringLiteral("Threads"),
                                                 .value = QStringLiteral("16"),
                                                 .requested = QStringLiteral("Auto"),
                                                 .reason = QStringLiteral("The host has sixteen cores.")}}});

    Reporting::ReportMaster master;
    master.key = QStringLiteral("64x48|-|-|-|Luminance|NGC 253|-");
    master.slug = QStringLiteral("luminance");
    master.name = QStringLiteral("Luminance");
    master.state = Reporting::ReportMasterState::Made;
    master.bytes = 12288;
    master.snapshot.backgroundNoise = 0.0125;
    master.snapshot.snr = 9.6;
    master.statistics =
        Reporting::ReportImageStatistics{.width = 64u, .height = 48u, .channels = 1u, .channelStats = {}};
    master.preview = QStringLiteral("report_previews/luminance/master.jpg");
    for (const bool used : {true, false})
    {
        Reporting::ReportFrame frame;
        frame.fileName = used ? QStringLiteral("light_001.fits") : QStringLiteral("light_002.fits");
        frame.path = QStringLiteral("lights/") + frame.fileName;
        frame.state = used ? Reporting::ReportFrameState::Used : Reporting::ReportFrameState::RegistrationFailed;
        frame.night = QDate(2026, 3, 23);
        frame.exposureSeconds = 180.0;
        frame.fwhm = used ? 2.8 : 6.1;
        frame.normalizationScale = {1.0, 0.98};
        master.frames.push_back(frame);
    }
    master.baseline =
        Reporting::ReportBaseline{.runId = QStringLiteral("20260324_081530_402"),
                                  .archivedAt = model.run.archivedAt->addDays(-2),
                                  .made = true,
                                  .frames = 1,
                                  .framesTaken = 1,
                                  .integrationMs = 180000,
                                  .backgroundNoise = 0.0181,
                                  .sideBySide = true,
                                  .preview = QStringLiteral("report_previews/luminance/previous_master.jpg")};
    model.masters.push_back(master);

    Reporting::ReportComparison comparison;
    comparison.baselineRunId = master.baseline->runId;
    comparison.baselineArchivedAt = master.baseline->archivedAt;
    comparison.current =
        Reporting::ReportRunTotals{.frames = 2, .integrationMs = 360000, .framesUsed = 1, .masters = 1};
    comparison.previous = Reporting::ReportRunTotals{.frames = 1, .integrationMs = 180000, .masters = 1, .snr = 6.4};
    comparison.settingsChanged.push_back({.stage = QStringLiteral("Stacking"),
                                          .groupKey = master.key,
                                          .group = master.name,
                                          .block = QStringLiteral("Stacking"),
                                          .name = QStringLiteral("Weighting"),
                                          .before = QStringLiteral("Noise"),
                                          .after = QStringLiteral("Weighted FWHM")});
    comparison.settingsCompared = 12;
    comparison.groupsChanged.push_back(
        {.stage = QStringLiteral("Registration"), .groupKey = QStringLiteral("reg"), .group = {}, .added = true});
    comparison.inputsChanged.push_back({.stage = QStringLiteral("Stacking"),
                                        .groupKey = master.key,
                                        .group = master.name,
                                        .added = {QStringLiteral("light_002.fits")},
                                        .removed = {}});
    model.comparison = comparison;
    model.history.push_back(
        Reporting::ReportHistoryEntry{.runId = master.baseline->runId,
                                      .refusal = {},
                                      .archivedAt = master.baseline->archivedAt,
                                      .outcome = Reporting::ReportOutcome::Completed,
                                      .framesUsed = 1,
                                      .frames = 1,
                                      .integrationMs = 180000,
                                      .masters = 1,
                                      .backgroundNoise = 0.0181,
                                      .report = QStringLiteral("../20260324_081530_402/workflow_report.html")});
    model.history.push_back(Reporting::ReportHistoryEntry{
        .runId = QStringLiteral("20260320_074410_251"),
        .refusal = QStringLiteral("The archive keeps no stored report (workflow_report.json).")});
    model.notStated.push_back({.what = QStringLiteral("The session's notes"), .why = QStringLiteral("None was kept.")});
    return model;
}

} // namespace

TargetInfo targetInfo()
{
    return {.name = "Fuzz_ConsumerDocuments", .maxInputBytes = 32768u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    std::vector<std::vector<std::uint8_t>> seeds;

    ImageIntegration::DocumentEnvelope envelope;
    envelope.documentType = QStringLiteral("normalization-coefficients");
    envelope.schemaVersion = 1;
    envelope.identity = QStringLiteral("0123456789abcdef");
    envelope.payload = QByteArrayLiteral("{\"mode\":\"global\"}");
    seeds.push_back(seed(Envelope, ImageIntegration::encodeDocumentEnvelope(envelope)));

    // A new project's session, as the window stores it, and the same session with no schema stated.
    const IntegrationDataModel model;
    const ImageIntegration::SessionDocumentResult stored =
        ImageIntegration::writeSessionDocument(model.toJson(), QStringLiteral("seed"));
    if (stored.ok())
    {
        seeds.push_back(seed(Session, QJsonDocument(*stored.document).toJson(QJsonDocument::Compact)));
        QJsonObject unversioned = *stored.document;
        unversioned.remove(QString::fromLatin1(ImageIntegration::kSessionSchemaKey));
        seeds.push_back(seed(Session, QJsonDocument(unversioned).toJson(QJsonDocument::Compact)));
    }
    seeds.push_back(seed(Session, QByteArrayLiteral("{\"sessionSchema\":3}")));

    seeds.push_back(seed(TransformRecord, stage_utils::serializeTransformJson(an::RegistrationTransform{}).toUtf8()));

    ImageIntegration::DrizzleDescriptorDocument descriptor;
    descriptor.frameId = QStringLiteral("light_001.fits");
    descriptor.sourceWidth = 64u;
    descriptor.sourceHeight = 48u;
    descriptor.outputWidth = 128u;
    descriptor.outputHeight = 96u;
    descriptor.scale = 2.0;
    descriptor.engineIdentity = QByteArrayLiteral("identity");
    seeds.push_back(seed(DrizzleDescriptor, ImageIntegration::formatDrizzleDescriptor(descriptor)));

    // A global set of two grey frames, the second matched to the first by the match that changes nothing.
    ImageIntegration::NormalizationCoefficientSet coefficients;
    coefficients.channelCount = 1u;
    coefficients.width = 64u;
    coefficients.height = 48u;
    coefficients.logicalFrames = {QStringLiteral("light_001.fits"), QStringLiteral("light_002.fits")};
    coefficients.sourceFrames = coefficients.logicalFrames;
    for (std::size_t frame = 0u; frame < coefficients.logicalFrames.size(); ++frame)
    {
        an::Result<an::FrameNormalization> identity =
            an::identityNormalization(an::NormalizationMethod::Additive, coefficients.channelCount);
        if (identity.hasValue())
        {
            coefficients.global.push_back(*identity);
        }
    }
    seeds.push_back(seed(Coefficients, ImageIntegration::formatNormalizationCoefficients(coefficients)));
    seeds.push_back(seed(Coefficients, QByteArrayLiteral("{}")));

    // A report as a run's archive keeps it; the same document of a format this version does not read; an object
    // that states no format; and a document cut short.
    const ImageIntegration::Reporting::ReportModel report = seedReport();
    const QJsonObject storedReport = ImageIntegration::Reporting::ReportModelJson::toJson(report);
    const QByteArray reportBytes = QJsonDocument(storedReport).toJson(QJsonDocument::Compact);
    seeds.push_back(seed(StoredReport, reportBytes));
    QJsonObject otherFormat = storedReport;
    otherFormat.insert(QStringLiteral("reportSchema"), ImageIntegration::Reporting::kReportSchema + 1);
    seeds.push_back(seed(StoredReport, QJsonDocument(otherFormat).toJson(QJsonDocument::Compact)));
    seeds.push_back(seed(StoredReport, QByteArrayLiteral("{}")));
    seeds.push_back(seed(StoredReport, reportBytes.left(reportBytes.size() / 2)));
    return seeds;
}

} // namespace an::fuzz

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);
    // The mode is taken from the front, so a seed's first byte names what follows it.
    const std::uint32_t mode = input.takeBelow(an::fuzz::ModeCount);
    const QByteArray bytes = QByteArray::fromStdString(input.restAsText());
    switch (mode)
    {
    case an::fuzz::Envelope:
        an::fuzz::checkEnvelope(bytes);
        break;
    case an::fuzz::Session:
        an::fuzz::checkSession(bytes);
        break;
    case an::fuzz::TransformRecord:
        an::fuzz::checkTransformRecord(bytes);
        break;
    case an::fuzz::DrizzleDescriptor:
        an::fuzz::checkDrizzleDescriptor(bytes);
        break;
    case an::fuzz::StoredReport:
        an::fuzz::checkStoredReport(bytes);
        break;
    default:
        an::fuzz::checkCoefficients(bytes);
        break;
    }
    return 0;
}
