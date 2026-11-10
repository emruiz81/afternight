// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#define NOMINMAX
#undef min
#undef max

#include "Test_ImageIntegration/reporting/ReportScenarios.h"

#include <gtest/gtest.h>

#include "Test_ImageIntegration/ImportSession.h"

#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTime>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <utility>

namespace an::tests::integration::report
{

using an::tests::fixtures::AcquisitionFacts;
using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureCardKind;
using an::tests::fixtures::FixtureChannels;

int countOccurrences(const QString& haystack, const QString& needle)
{
    if (needle.isEmpty())
    {
        return 0;
    }

    int count = 0;
    int from = 0;
    while (true)
    {
        from = haystack.indexOf(needle, from);
        if (from < 0)
        {
            break;
        }
        ++count;
        from += needle.size();
    }
    return count;
}

FixtureCard textCard(const std::string& keyword, const QString& value)
{
    return FixtureCard{keyword, value.toStdString(), {}, FixtureCardKind::Text};
}

bool writeFloatImage(const QString& path, int width, int height, FixtureChannels channels, double level,
                     const SampleRule& rule, std::vector<FixtureCard> cards)
{
    an::tests::fixtures::FixtureImage image;
    // A description takes plain letters, digits, dots, hyphens and underscores; the file keeps its own name.
    for (const QChar character : QFileInfo(path).completeBaseName())
    {
        const char16_t unit = character.unicode();
        const bool plain = (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') ||
                           (unit >= u'0' && unit <= u'9') || unit == u'.' || unit == u'-' || unit == u'_';
        image.name.push_back(plain ? static_cast<char>(unit) : '_');
    }
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.channels = channels;
    image.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
    image.pattern = an::tests::fixtures::FixturePattern::Constant;
    an::tests::fixtures::FixtureChannelLevels levels;
    levels.base = level;
    image.levels.assign(an::tests::fixtures::channelCountOf(channels), levels);
    image.cards = std::move(cards);

    std::string error;
    std::optional<an::tests::fixtures::FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return false;
    }
    if (rule)
    {
        for (std::uint32_t channel = 0u; channel < samples->channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < image.height; ++y)
            {
                for (std::uint32_t x = 0u; x < image.width; ++x)
                {
                    samples->setValue(x, y, channel, static_cast<double>(static_cast<float>(rule(x, y, channel))));
                }
            }
        }
    }
    const bool written =
        an::tests::fixtures::writeFixtureFits(image, *samples, std::filesystem::path(path.toStdWString()), error);
    EXPECT_TRUE(written) << error;
    return written;
}

std::vector<FixtureCard> equipmentCards(const AcquisitionFacts& facts, const QString& telescope, const QString& siteLat,
                                        const QString& siteLong)
{
    std::vector<FixtureCard> cards = an::tests::fixtures::acquisitionCards(facts);
    if (!telescope.trimmed().isEmpty())
    {
        cards.push_back(textCard("TELESCOP", telescope));
    }
    if (!siteLat.trimmed().isEmpty())
    {
        cards.push_back(textCard("SITELAT", siteLat));
    }
    if (!siteLong.trimmed().isEmpty())
    {
        cards.push_back(textCard("SITELONG", siteLong));
    }
    return cards;
}

QString writeMasterWithEquipmentMetadata(const QString& path, int width, int height, const QString& camera,
                                         const QString& telescope, bool isIso, int isoOrGain, int offset,
                                         const QString& siteLat, const QString& siteLong, const QString& objectName)
{
    AcquisitionFacts facts;
    facts.kind = {};
    facts.instrument = camera.toStdString();
    facts.object = objectName.toStdString();
    if (isIso)
    {
        facts.isoSpeed = isoOrGain;
    }
    else
    {
        facts.gain = isoOrGain;
    }
    facts.offset = offset;
    EXPECT_TRUE(writeFloatImage(path, width, height, FixtureChannels::Mono, 0.25, {},
                                equipmentCards(facts, telescope, siteLat, siteLong)));
    return path;
}

QString writeNoisyMaster(const QString& path, int width, int height, float baseLevel)
{
    // Simple deterministic pattern to ensure the background noise is not zero.
    const SampleRule pattern = [baseLevel](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        const float dx = static_cast<float>(x % 7u) * 0.001f;
        const float dy = static_cast<float>(y % 5u) * 0.001f;
        const float checker = ((x ^ y) & 1u) ? 0.002f : -0.002f;
        return static_cast<double>(baseLevel + dx + dy + checker);
    };
    EXPECT_TRUE(writeFloatImage(path, width, height, FixtureChannels::Mono, baseLevel, pattern,
                                {textCard("OBJECT", QStringLiteral("M83"))}));
    return path;
}

QString writeRgbMaster(const QString& path, int width, int height)
{
    const SampleRule pattern = [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        constexpr float channelBase[3] = {0.18f, 0.24f, 0.31f};
        const float offset = static_cast<float>((x + y) % 5u) * 0.004f;
        return static_cast<double>(channelBase[channel] + offset);
    };
    EXPECT_TRUE(
        writeFloatImage(path, width, height, FixtureChannels::Rgb, 0.0, pattern,
                        {textCard("OBJECT", QStringLiteral("M83 RGB")), textCard("FILTER", QStringLiteral("RGB"))}));
    return path;
}

QString writeLightFrameWithMetadata(const QString& path, const std::optional<double>& exposureSeconds,
                                    const std::optional<int>& isoOrGain, bool isoMode, const std::optional<int>& offset,
                                    const QString& objectName, const QString& telescope, const QString& siteLat,
                                    const QString& siteLong)
{
    AcquisitionFacts facts;
    if (exposureSeconds.has_value() && *exposureSeconds > 0.0)
    {
        facts.exposureSeconds = *exposureSeconds;
    }
    if (isoOrGain.has_value())
    {
        if (isoMode)
        {
            facts.isoSpeed = *isoOrGain;
        }
        else
        {
            facts.gain = *isoOrGain;
        }
    }
    if (offset.has_value())
    {
        facts.offset = *offset;
    }
    facts.object = objectName.trimmed().toStdString();
    EXPECT_TRUE(writeFloatImage(path, 16, 10, FixtureChannels::Mono, 0.2, {},
                                equipmentCards(facts, telescope, siteLat, siteLong)));
    return path;
}

std::optional<an::consumer::EngineSession> openReportSession()
{
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::move(session).value();
}

bool writeRegistrationMetricsDb(const QString& dbPath, const QString& groupKey, int settingsVersion,
                                const QList<QJsonObject>& rows)
{
    const QString connectionName =
        QStringLiteral("ReportTestDb_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dbPath);
        if (!db.open())
        {
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery createQuery(db);
        if (!createQuery.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS registration_metrics ("
                                             "frame_id TEXT NOT NULL,"
                                             "group_key TEXT NOT NULL,"
                                             "settings_version INTEGER NOT NULL,"
                                             "stars INTEGER NOT NULL,"
                                             "fwhm REAL NOT NULL,"
                                             "snr REAL NOT NULL,"
                                             "eccentricity REAL NOT NULL,"
                                             "photometric_zero_point REAL NOT NULL DEFAULT 0.0,"
                                             "zero_point_scatter REAL NOT NULL DEFAULT 0.0,"
                                             "astrometric_rms_pixels REAL NOT NULL DEFAULT 0.0,"
                                             "astrometric_rms_arcsec REAL NOT NULL DEFAULT 0.0,"
                                             "limiting_magnitude REAL NOT NULL DEFAULT 0.0,"
                                             "catalog_matched_stars INTEGER NOT NULL DEFAULT 0,"
                                             "succeeded INTEGER NOT NULL,"
                                             "output_path TEXT,"
                                             "transform_json TEXT,"
                                             "updated_at INTEGER NOT NULL,"
                                             "PRIMARY KEY (frame_id, group_key, settings_version))")))
        {
            db.close();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery insertQuery(db);
        if (!insertQuery.prepare(
                QStringLiteral("INSERT OR REPLACE INTO registration_metrics "
                               "(frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
                               "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, "
                               "astrometric_rms_arcsec, limiting_magnitude, catalog_matched_stars, "
                               "succeeded, output_path, transform_json, updated_at) "
                               "VALUES (:frame_id, :group_key, :settings_version, :stars, :fwhm, :snr, :eccentricity, "
                               ":photometric_zero_point, :zero_point_scatter, :astrometric_rms_pixels, "
                               ":astrometric_rms_arcsec, :limiting_magnitude, :catalog_matched_stars, "
                               ":succeeded, :output_path, :transform_json, :updated_at)")))
        {
            db.close();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        qint64 ts = 1;
        for (const auto& row : rows)
        {
            insertQuery.bindValue(QStringLiteral(":frame_id"), row.value(QStringLiteral("frame_id")).toString());
            insertQuery.bindValue(QStringLiteral(":group_key"), groupKey);
            insertQuery.bindValue(QStringLiteral(":settings_version"), settingsVersion);
            insertQuery.bindValue(QStringLiteral(":stars"), row.value(QStringLiteral("stars")).toInt());
            insertQuery.bindValue(QStringLiteral(":fwhm"), row.value(QStringLiteral("fwhm")).toDouble());
            insertQuery.bindValue(QStringLiteral(":snr"), row.value(QStringLiteral("snr")).toDouble());
            insertQuery.bindValue(QStringLiteral(":eccentricity"),
                                  row.value(QStringLiteral("eccentricity")).toDouble());
            insertQuery.bindValue(QStringLiteral(":photometric_zero_point"),
                                  row.contains(QStringLiteral("photometric_zero_point"))
                                      ? row.value(QStringLiteral("photometric_zero_point")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":zero_point_scatter"),
                                  row.contains(QStringLiteral("zero_point_scatter"))
                                      ? row.value(QStringLiteral("zero_point_scatter")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":astrometric_rms_pixels"),
                                  row.contains(QStringLiteral("astrometric_rms_pixels"))
                                      ? row.value(QStringLiteral("astrometric_rms_pixels")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":astrometric_rms_arcsec"),
                                  row.contains(QStringLiteral("astrometric_rms_arcsec"))
                                      ? row.value(QStringLiteral("astrometric_rms_arcsec")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":limiting_magnitude"),
                                  row.contains(QStringLiteral("limiting_magnitude"))
                                      ? row.value(QStringLiteral("limiting_magnitude")).toDouble()
                                      : 0.0);
            insertQuery.bindValue(QStringLiteral(":catalog_matched_stars"),
                                  row.contains(QStringLiteral("catalog_matched_stars"))
                                      ? row.value(QStringLiteral("catalog_matched_stars")).toInt()
                                      : 0);
            insertQuery.bindValue(QStringLiteral(":succeeded"),
                                  row.value(QStringLiteral("succeeded")).toBool() ? 1 : 0);
            insertQuery.bindValue(QStringLiteral(":output_path"), row.value(QStringLiteral("output_path")).toString());
            insertQuery.bindValue(QStringLiteral(":transform_json"),
                                  row.value(QStringLiteral("transform_json")).toString(QStringLiteral("{}")));
            insertQuery.bindValue(QStringLiteral(":updated_at"), ts++);
            if (!insertQuery.exec())
            {
                db.close();
                QSqlDatabase::removeDatabase(connectionName);
                return false;
            }
        }

        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

an::MatchedSourceReference makeSpccHandoffReference(std::uint64_t sourceId, double separationArcsec, double residualPx)
{
    an::MatchedSourceReference reference;
    reference.sourceIdentifier = sourceId;
    reference.astrometricCatalogFamilyId = "gaia_dr3";
    reference.astrometricCatalogManifestId = "gaia-dr3-main-2026-05";
    reference.spectrumCatalogFamilyId = "spcc_gaia_dr3_xp_sampled";
    reference.spectrumCatalogManifestId = "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    reference.wcsSolutionId = "iip-wcs-solution-42";
    reference.observationEpoch = "2025.25";
    reference.properMotionModelVersion = "gaia-dr3-pm-v1";
    reference.separationArcseconds = separationArcsec;
    reference.residualPixels = residualPx;
    return reference;
}

bool writeArchivedRunManifest(const QString& runDirectory, const WorkflowManifest& manifest,
                              const QString& overallResult, const QJsonArray& integrationGroupResults)
{
    if (!QDir().mkpath(runDirectory))
    {
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("overallResult"), overallResult);
    root.insert(QStringLiteral("manifest"), manifest.toJson());
    root.insert(QStringLiteral("integrationGroupResults"), integrationGroupResults);

    QFile file(QDir(runDirectory).filePath(QStringLiteral("workflow_manifest.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size();
}

namespace
{
/// The frames every scenario's camera takes, small so a suite builds a run in a moment.
constexpr int kFrameWidth = 64;
constexpr int kFrameHeight = 48;

/// The site's zone: three hours behind UTC all year, so a night's date is the same on every host.
constexpr int kSiteUtcOffsetSeconds = -3 * 3600;
const char kSiteTimeZoneId[] = "America/Argentina/Buenos_Aires";

/// The seconds a camera needs between two exposures.
constexpr int kDownloadSeconds = 9;

/// The input policy a stack states for a group that keeps its registered images.
const char kRetainedInputPolicy[] =
    "Materialized registered images (registered_output_retention: The group keeps its registered images.)";

/// What became of a master the run planned.
enum class MasterOutcome
{
    Made,
    Failed,
    NotReached,
    Reused,
};

/// Where a calibration master comes from.
enum class CalibrationOrigin
{
    Made,
    Reused,
    Supplied,
};

/// What became of one light.
enum class LightState
{
    Used,
    ExcludedByUser,
    StackExcludedByUser,
    RegistrationFailed,
    DroppedByStack,
    ExcludedOnRestack,
};

/// A number in [-1, 1] that depends only on its two seeds: the scatter of a measured figure, the same on every run.
[[nodiscard]] double scatter(std::uint32_t first, std::uint32_t second)
{
    std::uint32_t hash = first * 2654435761u ^ (second + 0x9E3779B9u + (first << 6) + (first >> 2));
    hash ^= hash >> 16;
    hash *= 0x7FEB352Du;
    hash ^= hash >> 15;
    hash *= 0x846CA68Bu;
    hash ^= hash >> 16;
    return static_cast<double>(hash) / 2147483647.5 - 1.0;
}

/// A text as a seed.
[[nodiscard]] std::uint32_t seedOf(const QString& text)
{
    return static_cast<std::uint32_t>(qHash(text, 0x5EED5EEDu));
}

/// A name with every space an underscore, as the workflow names an output after its target.
[[nodiscard]] QString underscored(QString name)
{
    return name.replace(QLatin1Char(' '), QLatin1Char('_'));
}

/// One light group of a run: what was taken, how it is calibrated and stacked, and what became of it.
struct LightGroupPlan
{
    QString target;

    /// The filter, or empty for a colour camera.
    QString filter;

    /// The frames' size, as a group key states it.
    int width = kFrameWidth;
    int height = kFrameHeight;

    /// The exposure of every light, or the exposures the lights cycle through.
    double exposureSeconds = 120.0;
    std::vector<double> exposureCycle;

    /// How many lights each night gave, by night.
    std::vector<int> lightsPerNight;

    /// When the group's first exposure of a night starts, in minutes after the night's first, or nothing for a
    /// group that follows the one before it. A group with a slot of its own takes the same lights at the same times
    /// whatever the other groups of the run take, which is what a later run of the same folder needs.
    std::optional<int> nightStartMinutes;

    /// The registration group, as `addRegistrationGroup` numbered it.
    int registrationGroup = 0;

    bool colour = false;
    std::optional<FrameId> bias;
    std::optional<FrameId> dark;
    std::optional<FrameId> flat;
    bool preCalibrated = false;
    WorkflowManifest::CalibrationSettings calibration;

    StackingSettings stacking;
    std::optional<WorkflowManifest::IntegrationGroup::DrizzleSettings> drizzle;

    MasterOutcome outcome = MasterOutcome::Made;
    MasterSolutionOutcome solution = MasterSolutionOutcome::None;

    /// How many of the group's lights met each fate other than being combined.
    int excludedByUser = 0;
    int stackExcludedByUser = 0;
    int registrationFailures = 0;
    int droppedByStack = 0;
    int excludedOnRestack = 0;

    /// Whether the registration matched the lights against a catalog: zero points, residuals and matches.
    bool photometry = false;

    /// Whether the stack wrote its rejection maps and its coverage map.
    bool auxiliaryMaps = false;

    /// The night's median star width in pixels, which each light scatters around.
    double seeing = 2.8;

    /// The master's level, a fraction of full scale.
    double masterLevel = 0.12;
};

/// One light of a run.
struct PlannedLight
{
    FrameId id;
    QString path;
    double exposureSeconds = 0.0;
    QDateTime captureUtc;
    LightState state = LightState::Used;
    double fwhm = 0.0;
};

/// A registration group being filled.
struct RegistrationPlan
{
    QString key;
    QString target;
    QString resolution;
    RegistrationSettings settings;
    std::vector<an::ui::RegistrationMetricRecord> metrics;
    int sipCorrected = 0;
    int caCorrected = 0;
    int caPassthrough = 0;
};

/// The six phases a stack's finalization reports, and what each took.
[[nodiscard]] StackGroupFinalizationTiming finalizationOf(const QString& failedPhase, std::uint32_t seed)
{
    static constexpr std::array<const char*, 6> kPhases = {
        "Final Master Save",           "Requested Maps",        "Final Master WCS", "Preview Generation",
        "Digest & Artifact Recording", "Completion Publication"};
    StackGroupFinalizationTiming timing;
    timing.captured = true;
    for (std::size_t phase = 0; phase < kPhases.size(); ++phase)
    {
        const QString name = QString::fromLatin1(kPhases[phase]);
        StagePhaseTiming entry;
        entry.stageName = name;
        if (name == failedPhase)
        {
            entry.status = QStringLiteral("failed");
            entry.durationMs = 40 + static_cast<int>(std::abs(scatter(seed, 90u)) * 30.0);
            entry.groupsProcessed = 1;
            timing.phases.push_back(entry);
            // A failed attempt reports the phases that ran and stops.
            break;
        }
        if (phase == 1)
        {
            entry.status = QStringLiteral("skipped");
        }
        else
        {
            entry.status = QStringLiteral("completed");
            entry.durationMs =
                static_cast<int>(20.0 + 110.0 * std::abs(scatter(seed, static_cast<std::uint32_t>(phase))));
            entry.groupsProcessed = 1;
        }
        timing.phases.push_back(entry);
    }
    return timing;
}

/// The positions of `count` lights among `total`, spread out and skipping the ones already taken.
void assignState(std::vector<PlannedLight>& lights, int count, LightState state, int start)
{
    const int total = static_cast<int>(lights.size());
    int placed = 0;
    // Every seventh light from the start, then - where the stride comes round to lights already taken - the first
    // free ones.
    for (int step = 0; step < total && placed < count; ++step)
    {
        PlannedLight& light = lights[static_cast<std::size_t>((start + step * 7) % total)];
        if (light.state == LightState::Used)
        {
            light.state = state;
            ++placed;
        }
    }
    for (int position = 0; position < total && placed < count; ++position)
    {
        PlannedLight& light = lights[static_cast<std::size_t>(position)];
        if (light.state == LightState::Used)
        {
            light.state = state;
            ++placed;
        }
    }
}
} // namespace

QStringList ReportScenario::madeMasterPaths() const
{
    QStringList paths;
    for (const IntegrationGroupResult& group : result.integrationGroupResults)
    {
        if (group.success && !group.outputPath.isEmpty())
        {
            paths.append(group.outputPath);
        }
    }
    return paths;
}

/// Builds one scenario: the runs of an output folder, one after another, the last of them the scenario's own.
class ReportScenarioBuilder
{
public:
    /// @param id the scenario.
    /// @param withSession whether a report of the run reads images and headers.
    explicit ReportScenarioBuilder(ReportScenarioId id, bool withSession = true)
    {
        m_scenario.id = id;
        m_scenario.name = reportScenarioName(id);
        m_scenario.m_root = std::make_shared<QTemporaryDir>();
        EXPECT_TRUE(m_scenario.m_root->isValid());
        if (!m_scenario.m_root->isValid())
        {
            m_usable = false;
            return;
        }
        m_scenario.outputDirectory = QDir(m_scenario.m_root->path()).filePath(QStringLiteral("Integration"));
        EXPECT_TRUE(QDir().mkpath(m_scenario.outputDirectory));

        // Every run reads its frames in a session; a scenario without one lets it go once its files are written.
        std::optional<an::consumer::EngineSession> session = openReportSession();
        if (!session.has_value())
        {
            m_usable = false;
            return;
        }
        m_session = std::make_shared<an::consumer::EngineSession>(std::move(*session));
        m_reportedWithSession = withSession;
    }

    [[nodiscard]] bool usable() const noexcept
    {
        return m_usable;
    }

    /// Starts a run of the output folder. The run before it, if any, must have been archived.
    void beginRun(const QString& executionId, ExecutionMode mode = ExecutionMode::Full)
    {
        m_manifest = WorkflowManifest{};
        m_result = WorkflowExecutor::Result{};
        m_facts = ReportScenarioFacts{};
        m_leftOut.clear();
        m_registration.clear();
        m_nightClocks.clear();
        m_log.clear();
        m_stackedGroups = 0;
        m_madeCalibrationByType = {0, 0, 0};

        m_manifest.executionId = executionId;
        m_manifest.outputDirectory = m_scenario.outputDirectory;
        m_manifest.executionMode = mode;
        m_manifest.maxParallelCores = 16;
        m_manifest.plateSolve.gaiaCatalogPack = QStringLiteral("gaia_dr3_g14");
        m_result.success = true;
    }

    /// Whether the lights' files are written. A run of thousands of lights names them and writes none.
    void setLightFilesWritten(bool written) noexcept
    {
        m_lightFilesWritten = written;
    }

    /// Whether the run's registration stored its metrics.
    void setMetricsStored(bool stored) noexcept
    {
        m_metricsStored = stored;
    }

    [[nodiscard]] WorkflowManifest& manifest() noexcept
    {
        return m_manifest;
    }

    [[nodiscard]] WorkflowExecutor::Result& result() noexcept
    {
        return m_result;
    }

    [[nodiscard]] ReportScenario& scenario() noexcept
    {
        return m_scenario;
    }

    [[nodiscard]] ReportScenarioFacts& facts() noexcept
    {
        return m_facts;
    }

    /// A path in the output folder.
    [[nodiscard]] QString outputPath(const QString& relative) const
    {
        return QDir(m_scenario.outputDirectory).filePath(relative);
    }

    /// A bias master of the run's camera.
    FrameId addBias(int frames, CalibrationOrigin origin = CalibrationOrigin::Made, int width = kFrameWidth,
                    int height = kFrameHeight)
    {
        const QString resolution = resolutionText(width, height);
        return addCalibrationMaster(m_manifest.biasStage, QStringLiteral("BIAS"), QStringLiteral("Bias"),
                                    QStringLiteral("%1|-|-|-").arg(resolution),
                                    QStringLiteral("master_bias_%1.fit").arg(resolution), QStringLiteral("biases"),
                                    frames, 0.0, origin, width, height, 0.02, {}, {});
    }

    /// A dark master of one exposure.
    FrameId addDark(double exposureSeconds, int frames, CalibrationOrigin origin = CalibrationOrigin::Made,
                    int width = kFrameWidth, int height = kFrameHeight)
    {
        const QString resolution = resolutionText(width, height);
        const QString exposure = QString::number(exposureSeconds);
        return addCalibrationMaster(m_manifest.darkStage, QStringLiteral("DARK"), QStringLiteral("Dark"),
                                    QStringLiteral("%1|-|-|%2|-").arg(resolution, exposure),
                                    QStringLiteral("master_dark_%1_~%2s.fit").arg(resolution, exposure),
                                    QStringLiteral("darks"), frames, exposureSeconds, origin, width, height, 0.025, {},
                                    {});
    }

    /// A flat master of one filter and session, calibrated with the masters given.
    FrameId addFlat(const QString& filter, const QString& session, int frames, const std::optional<FrameId>& bias,
                    const std::optional<FrameId>& dark, CalibrationOrigin origin = CalibrationOrigin::Made)
    {
        const QString resolution = resolutionText(kFrameWidth, kFrameHeight);
        const QString filterPart = filter.isEmpty() ? QStringLiteral("-") : filter;
        const QString sessionPart = session.isEmpty() ? QStringLiteral("-") : session;
        QString filename = QStringLiteral("master_flat_%1").arg(resolution);
        if (!filter.isEmpty())
        {
            filename += QLatin1Char('_') + filter;
        }
        if (!session.isEmpty())
        {
            filename += QLatin1Char('_') + session;
        }
        filename += QStringLiteral(".fit");
        if (!bias.has_value() && !dark.has_value() && origin == CalibrationOrigin::Made)
        {
            // The product's own sentence for a flat group it stacks uncalibrated.
            m_result.warnings.append(
                QStringLiteral("Flat group '%1|%2' has no bias or dark master assigned; master flat will be "
                               "uncalibrated.")
                    .arg(resolution, filterPart));
        }
        return addCalibrationMaster(m_manifest.flatStage, QStringLiteral("FLAT"), QStringLiteral("Flat"),
                                    QStringLiteral("%1|-|-|%2|-|%3").arg(resolution, filterPart, sessionPart), filename,
                                    QStringLiteral("flats"), frames, 2.5, origin, kFrameWidth, kFrameHeight, 0.45, bias,
                                    dark);
    }

    /// A registration group of one target and frame size.
    ///
    /// @return its number, which a light group names it by.
    int addRegistrationGroup(const QString& target, RegistrationSettings settings = {}, int width = kFrameWidth,
                             int height = kFrameHeight)
    {
        RegistrationPlan plan;
        plan.target = target;
        plan.resolution = resolutionText(width, height);
        plan.key = QStringLiteral("reg|%1|%2").arg(plan.resolution, target);
        plan.settings = std::move(settings);
        m_registration.push_back(std::move(plan));

        WorkflowManifest::RegistrationGroup group;
        group.groupKey = GroupKey{m_registration.back().key};
        group.resolution = m_registration.back().resolution;
        group.object = target;
        group.settings = m_registration.back().settings;
        m_manifest.registrationGroups.push_back(std::move(group));
        return static_cast<int>(m_registration.size()) - 1;
    }

    /// States how many of a registration group's lights the alignment corrected.
    void setAlignmentCorrections(int registrationGroup, int sipCorrected, int caCorrected, int caPassthrough)
    {
        RegistrationPlan& plan = m_registration[static_cast<std::size_t>(registrationGroup)];
        plan.sipCorrected = sipCorrected;
        plan.caCorrected = caCorrected;
        plan.caPassthrough = caPassthrough;
    }

    /// A group of lights: its frames, its calibration, its registration records, its stack and the master's fate.
    void addLightGroup(const LightGroupPlan& plan)
    {
        const QString resolution = resolutionText(plan.width, plan.height);
        const QString filterPart = plan.filter.isEmpty() ? QStringLiteral("-") : plan.filter;
        const QString key = QStringLiteral("%1|-|-|-|%2|%3|-").arg(resolution, filterPart, plan.target);
        QString outputFilename = QStringLiteral("stacked_%1").arg(resolution);
        if (!plan.filter.isEmpty())
        {
            outputFilename += QLatin1Char('_') + plan.filter;
        }
        outputFilename += QLatin1Char('_') + underscored(plan.target) + QStringLiteral(".fit");
        const std::uint32_t groupSeed = seedOf(key);

        std::vector<PlannedLight> lights = takeLights(plan, groupSeed);
        const int taken = static_cast<int>(lights.size());
        m_facts.lightsTaken += taken;

        if (taken > 0 && plan.excludedByUser >= taken)
        {
            // Every light left out: the grouping yields no group, and the manifest holds nothing of it.
            for (const PlannedLight& light : lights)
            {
                m_leftOut.push_back(ReportScenarioLeftOutLight{light.path, key, false});
            }
            m_facts.lightsLeftOutByUser += taken;
            ++m_facts.groupsLeftOutWhole;
            return;
        }

        assignState(lights, plan.excludedByUser, LightState::ExcludedByUser, 5);
        assignState(lights, plan.stackExcludedByUser, LightState::StackExcludedByUser, 11);
        assignState(lights, plan.registrationFailures, LightState::RegistrationFailed, 2);
        if (plan.outcome == MasterOutcome::Made)
        {
            assignState(lights, plan.droppedByStack, LightState::DroppedByStack, 4);
            assignState(lights, plan.excludedOnRestack, LightState::ExcludedOnRestack, 9);
        }

        RegistrationPlan& registration = m_registration[static_cast<std::size_t>(plan.registrationGroup)];
        WorkflowManifest::RegistrationGroup& registrationGroup =
            m_manifest.registrationGroups[static_cast<std::size_t>(plan.registrationGroup)];

        WorkflowManifest::IntegrationGroup group;
        group.groupKey = IntegrationGroupKey{GroupKey{key}};
        group.outputFilename = outputFilename;
        group.resolution = resolution;
        group.object = plan.target;
        group.filter = plan.filter;
        group.exposure = QStringLiteral("-");
        group.stacking = plan.stacking;
        group.drizzle = plan.drizzle;
        group.registrationGroupKey = GroupKey{registration.key};
        group.skipIfExists = plan.outcome == MasterOutcome::Reused;

        WorkflowManifest::PreCalibratedLightGroup preCalibrated;
        preCalibrated.groupKey = GroupKey{key};
        const bool calibrated = plan.bias.has_value() || plan.dark.has_value() || plan.flat.has_value();

        for (const PlannedLight& light : lights)
        {
            if (light.state == LightState::ExcludedByUser)
            {
                m_leftOut.push_back(ReportScenarioLeftOutLight{light.path, key, false});
                ++m_facts.lightsLeftOutByUser;
                continue;
            }

            registrationGroup.lightFiles.push_back(light.id);
            registrationGroup.lightExposureSeconds.push_back(light.exposureSeconds);
            registrationGroup.lightCaptureTimestampsUtc.push_back(light.captureUtc.toString(Qt::ISODateWithMs));
            registration.metrics.push_back(metricsOf(plan, light, registration));

            if (plan.preCalibrated)
            {
                preCalibrated.lightFiles.push_back(light.id);
            }
            else if (calibrated)
            {
                WorkflowManifest::LightsCalibrationParams entry;
                entry.lightPath = light.id;
                entry.biasPath = plan.bias;
                entry.darkPath = plan.dark;
                entry.flatPath = plan.flat;
                entry.settings = plan.calibration;
                m_manifest.lightsCalibration.push_back(std::move(entry));
            }

            if (light.state == LightState::StackExcludedByUser)
            {
                m_leftOut.push_back(ReportScenarioLeftOutLight{light.path, key, true});
                ++m_facts.lightsLeftOutByUser;
                continue;
            }
            if (light.state == LightState::RegistrationFailed)
            {
                ++m_facts.registrationFailures;
            }
            group.lightFiles.push_back(light.id);
            group.lightExposureSeconds.push_back(light.exposureSeconds);
        }
        if (plan.preCalibrated)
        {
            m_manifest.preCalibratedLightGroups.push_back(std::move(preCalibrated));
        }
        if (plan.calibration.cosmeticCorrection && !plan.dark.has_value() && plan.flat.has_value())
        {
            // The product's own sentence for a correction that has no dark to find defects in.
            const QString sentence =
                QStringLiteral("Cosmetic correction is on for the lights calibrated with the flat '%1', which have "
                               "no dark master to find defective pixels in, so none is corrected.")
                    .arg(QFileInfo(plan.flat->canonicalPath()).fileName());
            if (!m_result.warnings.contains(sentence))
            {
                m_result.warnings.append(sentence);
            }
        }

        const auto firstUsed = std::find_if(lights.begin(), lights.end(),
                                            [](const PlannedLight& light) { return light.state == LightState::Used; });
        if (plan.stacking.normalizationMode != NormalizationMode::None && firstUsed != lights.end())
        {
            group.normalizationReferenceFrames = {firstUsed->id};
            group.normalizationCoefficientsArtifactPath =
                outputPath(QStringLiteral(".afternight/image_integration/artifacts/normalization/%1/coefficients.json")
                               .arg(ImageIntegration::sanitizeFilename(key)));
        }

        const QString masterPath = outputPath(outputFilename);
        const int masterScale = plan.drizzle.has_value() && plan.drizzle->enabled ? 2 : 1;
        ++m_facts.mastersPlanned;
        switch (plan.outcome)
        {
        case MasterOutcome::Made:
            if (writeMaster(masterPath, plan, masterScale))
            {
                m_result.integrationGroupResults.push_back(
                    madeResult(plan, group, lights, masterPath, registration.key, groupSeed));
                m_result.generatedLightMasters.append(masterPath);
                ++m_facts.mastersMade;
                ++m_stackedGroups;
            }
            break;
        case MasterOutcome::Failed: {
            IntegrationGroupResult failed;
            failed.groupKey = key;
            failed.outputFilename = outputFilename;
            failed.registrationGroupKey = registration.key;
            failed.registrationInputPolicy = QString::fromLatin1(kRetainedInputPolicy);
            failed.lightFrameCount = static_cast<int>(group.lightFiles.size());
            failed.durationMs = 2600 + static_cast<int>(std::abs(scatter(groupSeed, 70u)) * 900.0);
            failed.success = false;
            failed.finalizationTiming = finalizationOf(QStringLiteral("Final Master Save"), groupSeed);
            m_result.integrationGroupResults.push_back(std::move(failed));
            m_result.errors.append(
                QStringLiteral("Stacking group '%1' failed: the master '%2' could not be written (the disk is full).")
                    .arg(key, outputFilename));
            ++m_facts.mastersFailed;
            ++m_stackedGroups;
            break;
        }
        case MasterOutcome::NotReached:
            ++m_facts.mastersNotReached;
            break;
        case MasterOutcome::Reused:
            // The master an earlier run made is still valid: the stage returns before any result is made.
            if (!QFileInfo::exists(masterPath))
            {
                (void)writeMaster(masterPath, plan, masterScale);
            }
            ++m_facts.mastersReused;
            break;
        }

        if (plan.outcome == MasterOutcome::Made || plan.outcome == MasterOutcome::Failed)
        {
            m_manifest.dirtyGroups.push_back(GroupKey{key});
            m_manifest.incrementalIntegrationGroups.push_back(group.groupKey);
        }
        m_manifest.integrationGroups.push_back(std::move(group));
    }

    /// The combined RGB output of a registration group's red, green and blue masters.
    void addCombinedRgb(int registrationGroup)
    {
        const RegistrationPlan& registration = m_registration[static_cast<std::size_t>(registrationGroup)];
        m_manifest.combineRgbImages = true;

        IntegrationGroupResult rgb;
        rgb.groupKey = ImageIntegration::combinedRgbGroupKey(registration.key);
        rgb.outputFilename = ImageIntegration::combinedRgbOutputFilename(registration.key);
        rgb.outputPath = outputPath(rgb.outputFilename);
        rgb.registrationGroupKey = registration.key;
        for (const IntegrationGroupResult& channel : m_result.integrationGroupResults)
        {
            const bool isChannel = channel.registrationGroupKey == registration.key && channel.success &&
                                   (channel.groupKey.contains(QStringLiteral("|Red|")) ||
                                    channel.groupKey.contains(QStringLiteral("|Green|")) ||
                                    channel.groupKey.contains(QStringLiteral("|Blue|")));
            if (isChannel)
            {
                rgb.lightFrameCount += channel.lightFrameCount;
            }
        }

        const SampleRule pattern = [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            constexpr std::array<double, 3> kChannelLevel = {0.10, 0.12, 0.15};
            return kChannelLevel[channel] + 0.003 * static_cast<double>((x * 3u + y * 5u + channel) % 11u);
        };
        if (!writeFloatImage(rgb.outputPath, kFrameWidth, kFrameHeight, FixtureChannels::Rgb, 0.0, pattern,
                             masterCards(registration.target, QStringLiteral("RGB"), true)))
        {
            m_usable = false;
            return;
        }
        rgb.outputBytes = QFileInfo(rgb.outputPath).size();
        rgb.masterSolution = MasterSolutionOutcome::Propagated;
        rgb.masterSolutionDetail = QStringLiteral("Carried from the red master's solution.");
        rgb.durationMs = 640;
        rgb.success = true;
        rgb.finalizationTiming = finalizationOf(QString(), seedOf(rgb.groupKey));
        m_result.generatedLightMasters.append(rgb.outputPath);
        m_result.integrationGroupResults.push_back(std::move(rgb));
        ++m_facts.mastersPlanned;
        ++m_facts.mastersMade;
    }

    /// States that the run stopped: failed, or cancelled by the person.
    void setStopped(bool cancelled, const QString& message)
    {
        m_result.success = false;
        m_result.cancelled = cancelled;
        m_result.partialSuccess = m_facts.mastersMade > 0;
        m_result.errorMessage = message;
    }

    /// Completes the run: its stages and timings, its log, its metrics database and the archive's context.
    void completeRun()
    {
        if (!m_usable)
        {
            return;
        }
        m_facts.registrationGroups = static_cast<int>(m_registration.size());
        m_facts.nights = static_cast<int>(m_nightClocks.size());
        for (const IntegrationGroupResult& group : m_result.integrationGroupResults)
        {
            // A combined RGB output combines no light of its own.
            if (group.success && group.qualityReport.valid)
            {
                m_facts.lightsCombined += group.lightFrameCount;
            }
        }

        writeMetricsDatabase();
        completeStages();

        const QDateTime archivedUtc(QDate(2026, 3, 26), QTime(10, 12, 4, 118), QTimeZone::UTC);
        const QDateTime archivedLocal = archivedUtc.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(kSiteUtcOffsetSeconds));
        QJsonObject context;
        context.insert(QStringLiteral("appVersion"), QStringLiteral("1.1.0"));
        context.insert(QStringLiteral("executionId"), m_manifest.executionId);
        context.insert(QStringLiteral("archivedAtUtc"), archivedUtc.toString(Qt::ISODateWithMs));
        context.insert(QStringLiteral("archivedAtLocal"),
                       archivedLocal.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")));
        context.insert(QStringLiteral("timeZoneId"), QString::fromLatin1(kSiteTimeZoneId));
        context.insert(QStringLiteral("utcOffsetSeconds"), kSiteUtcOffsetSeconds);
        m_scenario.context = context;

        m_result.warningCount = static_cast<int>(m_result.warnings.size());
        m_scenario.manifest = m_manifest;
        m_scenario.result = m_result;
        m_scenario.facts = m_facts;
        m_scenario.lightsLeftOutByUser = m_leftOut;
        m_scenario.runDirectory =
            outputPath(QStringLiteral("history/%1").arg(ImageIntegration::sanitizeFilename(m_manifest.executionId)));
    }

    /// Completes the run and archives it as the executor does, so the next run of the folder finds it in the history.
    [[nodiscard]] bool archiveAsEarlierRun()
    {
        completeRun();
        if (!m_usable)
        {
            return false;
        }
        m_scenario.m_session = m_session;
        const bool archived = archiveScenarioRun(m_scenario);
        m_scenario.m_session.reset();
        if (!archived)
        {
            m_usable = false;
            return false;
        }
        m_scenario.earlierRunDirectories.append(m_scenario.runDirectory);
        return true;
    }

    /// Completes the scenario's own run.
    [[nodiscard]] std::optional<ReportScenario> finish()
    {
        completeRun();
        if (!m_usable)
        {
            return std::nullopt;
        }
        if (m_reportedWithSession)
        {
            m_scenario.m_session = m_session;
        }
        return m_scenario;
    }

private:
    [[nodiscard]] static QString resolutionText(int width, int height)
    {
        return QStringLiteral("%1x%2").arg(width).arg(height);
    }

    /// The cards a master carries: the camera and its settings, the target, the filter, the telescope and the site.
    [[nodiscard]] static std::vector<FixtureCard> masterCards(const QString& target, const QString& filter, bool colour)
    {
        AcquisitionFacts facts;
        facts.kind = {};
        facts.instrument = colour ? "ZWO ASI2600MC Pro" : "ZWO ASI2600MM Pro";
        facts.object = target.toStdString();
        facts.filter = filter.toStdString();
        facts.gain = 100;
        facts.offset = 50;
        return equipmentCards(facts, QStringLiteral("Esprit 100ED"), QStringLiteral("-34.6037"),
                              QStringLiteral("-58.3816"));
    }

    /// Adds a calibration group to its stage, with its master's file and its result.
    FrameId addCalibrationMaster(std::optional<WorkflowManifest::CalibrationStage>& stage, const QString& frameType,
                                 const QString& resultType, const QString& key, const QString& outputFilename,
                                 const QString& folder, int frames, double exposureSeconds, CalibrationOrigin origin,
                                 int width, int height, double level, const std::optional<FrameId>& bias,
                                 const std::optional<FrameId>& dark)
    {
        if (!stage.has_value())
        {
            stage = WorkflowManifest::CalibrationStage{};
            stage->frameType = frameType;
        }

        WorkflowManifest::CalibrationStage::Group group;
        group.groupKey = GroupKey{key};
        group.outputFilename = outputFilename;
        group.stacking.normalizationMode =
            resultType == QStringLiteral("Flat") ? NormalizationMode::Global : NormalizationMode::None;
        group.biasMaster = bias;
        group.darkMaster = dark;

        QString masterPath = outputPath(outputFilename);
        const QString inputFolder =
            QDir(m_scenario.m_root->path()).filePath(QStringLiteral("calibration/%1").arg(folder));
        if (origin == CalibrationOrigin::Supplied)
        {
            // A master the person loaded: the group is that one file, kept where the person keeps it.
            masterPath = QDir(m_scenario.m_root->path())
                             .filePath(QStringLiteral("library/%1")
                                           .arg(QString(outputFilename)
                                                    .replace(QStringLiteral("master_"), QStringLiteral("library_"))));
            group.inputFiles = {FrameId(masterPath)};
            group.existingMaster = FrameId(masterPath);
            group.skipIfExists = true;
        }
        else
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                group.inputFiles.push_back(
                    FrameId(QStringLiteral("%1/%2_%3_%4s_%5.fits")
                                .arg(inputFolder, folder, ImageIntegration::sanitizeFilename(key))
                                .arg(exposureSeconds, 0, 'f', 2)
                                .arg(frame, 4, 10, QLatin1Char('0'))));
            }
            group.skipIfExists = origin == CalibrationOrigin::Reused;
        }

        if (!QFileInfo::exists(masterPath))
        {
            EXPECT_TRUE(QDir().mkpath(QFileInfo(masterPath).absolutePath()));
            const std::uint32_t seed = seedOf(key);
            const SampleRule pattern = [level, seed](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                return level * (1.0 + 0.004 * scatter(seed + x, y));
            };
            AcquisitionFacts facts;
            facts.kind = (QStringLiteral("Master ") + resultType).toStdString();
            facts.instrument = "ZWO ASI2600MM Pro";
            facts.gain = 100;
            facts.offset = 50;
            if (exposureSeconds > 0.0)
            {
                facts.exposureSeconds = exposureSeconds;
            }
            if (!writeFloatImage(masterPath, width, height, FixtureChannels::Mono, level, pattern,
                                 an::tests::fixtures::acquisitionCards(facts)))
            {
                m_usable = false;
            }
        }

        if (origin != CalibrationOrigin::Reused)
        {
            // A group whose master is still valid is skipped, and the run states no result for it.
            WorkflowExecutor::Result::CalibrationGroupResult result;
            result.type = resultType;
            result.groupKey = key;
            result.masterPath = masterPath;
            result.inputCount = static_cast<int>(group.inputFiles.size());
            result.success = true;
            m_result.calibrationGroupResults.push_back(std::move(result));
        }
        const std::size_t typeIndex =
            resultType == QStringLiteral("Bias") ? 0u : (resultType == QStringLiteral("Dark") ? 1u : 2u);
        if (origin == CalibrationOrigin::Made)
        {
            ++m_madeCalibrationByType[typeIndex];
            QStringList& generated =
                typeIndex == 0u ? m_result.generatedBiasMasters
                                : (typeIndex == 1u ? m_result.generatedDarkMasters : m_result.generatedFlatMasters);
            generated.append(masterPath);
        }
        ++m_facts.calibrationMasters;
        stage->groups.push_back(std::move(group));
        return FrameId(masterPath);
    }

    /// The clock of a night: when its next exposure starts, in UTC.
    [[nodiscard]] QDateTime& nightClock(int night)
    {
        if (static_cast<int>(m_nightClocks.size()) <= night)
        {
            m_nightClocks.resize(static_cast<std::size_t>(night) + 1);
        }
        QDateTime& clock = m_nightClocks[static_cast<std::size_t>(night)];
        if (!clock.isValid())
        {
            clock = nightStart(night);
        }
        return clock;
    }

    /// When a night's first exposure starts, in UTC: twenty to ten in the evening at the site.
    [[nodiscard]] static QDateTime nightStart(int night)
    {
        static constexpr std::array<int, 4> kEveningDays = {23, 24, 27, 28};
        const QDate evening(2026, 3, kEveningDays[static_cast<std::size_t>(night) % kEveningDays.size()]);
        return QDateTime(evening, QTime(21, 40, 0), QTimeZone::fromSecondsAheadOfUtc(kSiteUtcOffsetSeconds)).toUTC();
    }

    /// Takes a group's lights night by night, and writes their files when the run writes them.
    [[nodiscard]] std::vector<PlannedLight> takeLights(const LightGroupPlan& plan, std::uint32_t groupSeed)
    {
        std::vector<PlannedLight> lights;
        int index = 0;
        for (int night = 0; night < static_cast<int>(plan.lightsPerNight.size()); ++night)
        {
            const int count = plan.lightsPerNight[static_cast<std::size_t>(night)];
            if (count <= 0)
            {
                continue;
            }
            QDateTime& sharedClock = nightClock(night);
            QDateTime ownClock = nightStart(night).addSecs(qint64{60} * plan.nightStartMinutes.value_or(0));
            QDateTime& clock = plan.nightStartMinutes.has_value() ? ownClock : sharedClock;
            const QDate evening =
                clock.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(kSiteUtcOffsetSeconds)).addSecs(-12 * 3600).date();
            const QString folder =
                QDir(m_scenario.m_root->path())
                    .filePath(QStringLiteral("lights/%1/%2/LIGHT")
                                  .arg(underscored(plan.target), evening.toString(QStringLiteral("yyyy-MM-dd"))));
            if (m_lightFilesWritten)
            {
                EXPECT_TRUE(QDir().mkpath(folder));
            }

            for (int frame = 0; frame < count; ++frame, ++index)
            {
                PlannedLight light;
                light.exposureSeconds =
                    plan.exposureCycle.empty()
                        ? plan.exposureSeconds
                        : plan.exposureCycle[static_cast<std::size_t>(index) % plan.exposureCycle.size()];
                light.captureUtc = clock;
                clock = clock.addMSecs(static_cast<qint64>(light.exposureSeconds * 1000.0) + kDownloadSeconds * 1000);

                // The seeing drifts over a night and each light scatters around it.
                const double progress = count > 1 ? static_cast<double>(frame) / static_cast<double>(count - 1) : 0.0;
                light.fwhm = plan.seeing + 0.35 * std::sin(progress * 3.1) + 0.25 * static_cast<double>(night) +
                             0.18 * scatter(groupSeed, static_cast<std::uint32_t>(index));

                const QDateTime local =
                    light.captureUtc.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(kSiteUtcOffsetSeconds));
                light.path = QStringLiteral("%1/%2_%3_-10.00_%4s_%5.fits")
                                 .arg(folder, local.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")), plan.filter)
                                 .arg(light.exposureSeconds, 0, 'f', 2)
                                 .arg(index, 4, 10, QLatin1Char('0'));
                light.id = FrameId(light.path);
                if (m_lightFilesWritten && !writeLight(plan, light))
                {
                    m_usable = false;
                }
                lights.push_back(std::move(light));
            }
        }
        return lights;
    }

    /// Writes one light as a capture program writes it: its kind, exposure, temperature, gain, offset, filter,
    /// target, camera, start, telescope and site.
    [[nodiscard]] bool writeLight(const LightGroupPlan& plan, const PlannedLight& light) const
    {
        AcquisitionFacts facts;
        facts.kind = "Light Frame";
        facts.exposureSeconds = light.exposureSeconds;
        facts.sensorTemperatureCelsius = -10.0;
        facts.gain = 100;
        facts.offset = 50;
        facts.filter = plan.filter.toStdString();
        facts.object = plan.target.toStdString();
        facts.instrument = plan.colour ? "ZWO ASI2600MC Pro" : "ZWO ASI2600MM Pro";
        facts.exposureStart = light.captureUtc.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")).toStdString();

        std::string name;
        for (const QChar character : QFileInfo(light.path).completeBaseName())
        {
            const char16_t unit = character.unicode();
            const bool plain = (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') ||
                               (unit >= u'0' && unit <= u'9') || unit == u'.' || unit == u'-' || unit == u'_';
            name.push_back(plain ? static_cast<char>(unit) : '_');
        }
        an::tests::fixtures::FixtureImage frame = an::tests::fixtures::acquisitionFrame(
            name, facts, static_cast<std::uint32_t>(plan.width), static_cast<std::uint32_t>(plan.height));
        frame.cards.push_back(textCard("TELESCOP", QStringLiteral("Esprit 100ED")));
        frame.cards.push_back(textCard("SITELAT", QStringLiteral("-34.6037")));
        frame.cards.push_back(textCard("SITELONG", QStringLiteral("-58.3816")));

        std::string error;
        const bool written =
            an::tests::fixtures::writeAcquisitionFrame(frame, std::filesystem::path(light.path.toStdWString()), error);
        EXPECT_TRUE(written) << error;
        return written;
    }

    /// What the registration stored for one light.
    [[nodiscard]] an::ui::RegistrationMetricRecord metricsOf(const LightGroupPlan& plan, const PlannedLight& light,
                                                             const RegistrationPlan& registration) const
    {
        const std::uint32_t seed = seedOf(light.path);
        an::ui::RegistrationMetricRecord record;
        record.frameId = light.id;
        record.groupKey = registration.key;
        record.settingsVersion = registration.settings.version;
        record.updatedAt = 1774500000 + static_cast<qint64>(registration.metrics.size());

        if (light.state == LightState::RegistrationFailed)
        {
            // Clouds: a handful of stars, and no transform.
            record.metrics.stars = 9 + static_cast<int>(std::abs(scatter(seed, 1u)) * 6.0);
            record.metrics.fwhm = light.fwhm + 2.4;
            record.metrics.snr = 4.0 + std::abs(scatter(seed, 2u));
            record.metrics.eccentricity = 0.71;
            record.metrics.succeeded = false;
            record.noise.background_noise = 0.0052;
            record.noise.global_mad = 0.0044;
            record.noise.global_scale = 0.0066;
            return record;
        }

        record.metrics.fwhm = light.fwhm;
        record.metrics.stars = static_cast<int>(820.0 - 140.0 * (light.fwhm - plan.seeing) + 60.0 * scatter(seed, 3u));
        record.metrics.snr = 42.0 - 6.0 * (light.fwhm - plan.seeing) + 3.0 * scatter(seed, 4u);
        record.metrics.eccentricity = 0.34 + 0.07 * scatter(seed, 5u);
        record.metrics.succeeded = true;
        if (plan.photometry)
        {
            record.metrics.photometric_zero_point = 24.62 + 0.12 * scatter(seed, 6u);
            record.metrics.zero_point_scatter = 0.031 + 0.008 * std::abs(scatter(seed, 7u));
            record.metrics.astrometric_rms_pixels = 0.21 + 0.05 * std::abs(scatter(seed, 8u));
            record.metrics.astrometric_rms_arcsec = record.metrics.astrometric_rms_pixels * 1.61;
            record.metrics.limiting_magnitude = 17.8 + 0.3 * scatter(seed, 9u);
            record.metrics.catalog_matched_stars = 140 + static_cast<int>(30.0 * scatter(seed, 10u));
        }
        record.noise.background_noise = 0.0021 * (1.0 + 0.08 * scatter(seed, 11u));
        record.noise.global_mad = 0.0018 * (1.0 + 0.08 * scatter(seed, 12u));
        record.noise.global_scale = 0.0027 * (1.0 + 0.08 * scatter(seed, 13u));

        // A dither of a few pixels around the reference, which is the group's first light and moves nowhere.
        const bool reference = registration.metrics.empty();
        const double shiftX = reference ? 0.0 : 6.0 * scatter(seed, 14u);
        const double shiftY = reference ? 0.0 : 6.0 * scatter(seed, 15u);
        an::RegistrationTransformRequest request;
        request.linearKind = an::RegistrationTransformKind::Similarity;
        request.sourceToReference = {1.0, 0.0, shiftX, 0.0, 1.0, shiftY, 0.0, 0.0, 1.0};
        const an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(request);
        EXPECT_TRUE(transform.hasValue()) << transform.status().message();
        if (transform.hasValue())
        {
            record.transformJson = stage_utils::serializeTransformJson(
                *transform, stage_utils::SourceDistortionProvenance::explicitNone());
        }

        if (light.state != LightState::DroppedByStack)
        {
            // The registered image the alignment wrote; a light the stack drops has none.
            record.outputPath = outputPath(QStringLiteral("registered_lights/%1/%2_reg.fits")
                                               .arg(ImageIntegration::sanitizeFilename(registration.key),
                                                    QFileInfo(light.path).completeBaseName()));
            record.outputSizeBytes = static_cast<qint64>(plan.width) * plan.height * (plan.colour ? 12 : 4) + 5760;
            record.outputModifiedAtMs = 1774500000000 + static_cast<qint64>(registration.metrics.size()) * 1000;
        }
        return record;
    }

    /// Writes a master as the stack writes it, at the frames' size times the drizzle's scale.
    [[nodiscard]] bool writeMaster(const QString& path, const LightGroupPlan& plan, int scale)
    {
        const std::uint32_t seed = seedOf(path);
        const double level = plan.masterLevel;
        const bool colour = plan.colour;
        const SampleRule pattern = [level, seed, colour](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            // A faint gradient, a bright patch near the centre and noise, so every statistic has something to say.
            const double channelLevel = level * (colour ? 1.0 + 0.15 * static_cast<double>(channel) : 1.0);
            const double dx = static_cast<double>(x) - 30.0;
            const double dy = static_cast<double>(y) - 22.0;
            const double patch = 0.35 * std::exp(-(dx * dx + dy * dy) / 90.0);
            return channelLevel + 0.0004 * static_cast<double>(x) + patch + 0.0025 * scatter(seed + x + channel, y);
        };
        const bool written = writeFloatImage(path, plan.width * scale, plan.height * scale,
                                             colour ? FixtureChannels::Rgb : FixtureChannels::Mono, level, pattern,
                                             masterCards(plan.target, plan.filter, colour));
        if (!written)
        {
            m_usable = false;
        }
        return written;
    }

    /// The result of a master the run made, with the stack's own record of what it combined.
    [[nodiscard]] IntegrationGroupResult madeResult(const LightGroupPlan& plan,
                                                    const WorkflowManifest::IntegrationGroup& group,
                                                    const std::vector<PlannedLight>& lights, const QString& masterPath,
                                                    const QString& registrationKey, std::uint32_t groupSeed)
    {
        IntegrationGroupResult result;
        result.groupKey = group.groupKey.toString();
        result.outputPath = masterPath;
        result.outputFilename = group.outputFilename;
        result.outputBytes = QFileInfo(masterPath).size();
        result.registrationGroupKey = registrationKey;
        result.registrationInputPolicy = QString::fromLatin1(kRetainedInputPolicy);
        result.masterSolution = plan.solution;
        switch (plan.solution)
        {
        case MasterSolutionOutcome::Propagated:
            result.masterSolutionDetail =
                QStringLiteral("Carried from the registration group's output plane (%1).").arg(registrationKey);
            break;
        case MasterSolutionOutcome::Solved:
            result.masterSolutionDetail = QStringLiteral("Solved by the internal Gaia solver: 412 stars matched.");
            break;
        case MasterSolutionOutcome::None:
            result.masterSolutionDetail =
                QStringLiteral("Saved %1 without WCS because no registration output-plane WCS was available and "
                               "fallback plate solving is disabled.")
                    .arg(group.outputFilename);
            break;
        case MasterSolutionOutcome::NotRecorded:
            break;
        }
        result.success = true;
        result.finalizationTiming = finalizationOf(QString(), groupSeed);

        // What the stack combined, in the order it read the lights: the group's order, less the ones it left out.
        StackQualityRecord& quality = result.qualityReport;
        std::vector<const PlannedLight*> combined;
        QStringList dropped;
        std::size_t groupIndex = 0;
        for (const PlannedLight& light : lights)
        {
            if (light.state == LightState::ExcludedByUser || light.state == LightState::StackExcludedByUser)
            {
                continue;
            }
            if (light.state == LightState::ExcludedOnRestack)
            {
                quality.spatialRestackExcludedFrameIndices.push_back(groupIndex);
                quality.spatialRestackExcludedFrameRates.push_back(0.082 +
                                                                   0.03 * std::abs(scatter(seedOf(light.path), 20u)));
            }
            else if (light.state == LightState::DroppedByStack)
            {
                dropped.append(QStringLiteral("%1 (no registered output)").arg(light.id.canonicalPath()));
            }
            else if (light.state == LightState::Used)
            {
                combined.push_back(&light);
            }
            ++groupIndex;
        }
        if (!dropped.isEmpty())
        {
            // The stage's own sentence, which is the only place a dropped light is named.
            m_result.warnings.append(QStringLiteral("Skipping %1 light frame(s): %2")
                                         .arg(dropped.size())
                                         .arg(dropped.join(QStringLiteral("; "))));
            m_facts.lightsDroppedByStack += static_cast<int>(dropped.size());
        }
        m_facts.lightsExcludedOnRestack += static_cast<int>(quality.spatialRestackExcludedFrameIndices.size());

        const int count = static_cast<int>(combined.size());
        result.lightFrameCount = count;
        result.durationMs = 1800 + count * 35 + static_cast<int>(std::abs(scatter(groupSeed, 30u)) * 400.0);

        quality.valid = true;
        quality.inputFrameCount = static_cast<std::uint64_t>(count);
        quality.theoreticalSnrGain = std::sqrt(static_cast<double>(count));
        quality.minCoverage = (std::max)(1.0, static_cast<double>(count - count / 6));
        quality.medianCoverage = static_cast<double>(count);
        quality.maxCoverage = static_cast<double>(count);
        quality.coverageStddev = count > 1 ? 0.8 : 0.0;

        const bool weighted =
            plan.stacking.weighting.compare(QStringLiteral("None"), Qt::CaseInsensitive) != 0 && count > 0;
        std::vector<double> weights(static_cast<std::size_t>(count), 1.0);
        if (weighted)
        {
            // Sharper lights weigh more, at a mean of one.
            double sum = 0.0;
            for (std::size_t light = 0; light < combined.size(); ++light)
            {
                weights[light] = 1.0 / (combined[light]->fwhm * combined[light]->fwhm);
                sum += weights[light];
            }
            const double mean = sum / static_cast<double>(count);
            double sumOfSquares = 0.0;
            for (double& weight : weights)
            {
                weight /= mean;
                sumOfSquares += weight * weight;
            }
            const auto [lowest, highest] = std::minmax_element(weights.begin(), weights.end());
            quality.hasWeightingStats = true;
            quality.weightMin = *lowest;
            quality.weightMax = *highest;
            quality.weightStddev = std::sqrt((std::max)(0.0, sumOfSquares / static_cast<double>(count) - 1.0));
            quality.weightEfficiency = static_cast<double>(count) / sumOfSquares;
        }
        quality.effectiveFrameCount = static_cast<double>(count) * (weighted ? quality.weightEfficiency : 0.97);
        quality.actualSnrGain = std::sqrt(quality.effectiveFrameCount);
        quality.integrationEfficiency =
            quality.theoreticalSnrGain > 0.0 ? quality.actualSnrGain / quality.theoreticalSnrGain : 0.0;
        for (std::size_t light = 0; light < combined.size(); ++light)
        {
            quality.lightWeights.push_back(
                StackLightWeight{.light = combined[light]->id.canonicalPath(), .weight = weights[light]});
        }

        const bool rejects = plan.stacking.rejectionMethod.compare(QStringLiteral("None"), Qt::CaseInsensitive) != 0;
        if (rejects && count > 0)
        {
            quality.hasRejectionStats = true;
            for (const PlannedLight* light : combined)
            {
                quality.frameRejectionRates.push_back(0.011 + 0.004 * std::abs(scatter(seedOf(light->path), 21u)));
            }
            std::vector<double> sorted = quality.frameRejectionRates;
            std::sort(sorted.begin(), sorted.end());
            quality.minFrameRejectionRate = sorted.front();
            quality.maxFrameRejectionRate = sorted.back();
            quality.medianFrameRejectionRate = sorted[sorted.size() / 2];
            quality.meanFrameRejectionRate =
                std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
            quality.globalRejectionRate = quality.meanFrameRejectionRate;
        }

        if (plan.stacking.normalizationMode != NormalizationMode::None && !combined.empty())
        {
            quality.normalization = QStringLiteral("The normalization stage's coefficients, reference %1")
                                        .arg(QFileInfo(combined.front()->path).fileName());
        }
        if (plan.stacking.localWeighting.enabled)
        {
            quality.hasLocalWeightingStats = true;
            quality.localWeightWindowSize = plan.stacking.localWeighting.windowSize;
            quality.localWeightGridSpacing = plan.stacking.localWeighting.gridSpacing;
            quality.localWeightSnrExponent = plan.stacking.localWeighting.snrWeightExponent;
            quality.localWeightGridCount = 12;
            quality.localWeightMean = 1.0;
            quality.localWeightMin = 0.62;
            quality.localWeightMax = 1.41;
            quality.localWeightStddev = 0.19;
        }
        if (!quality.spatialRestackExcludedFrameIndices.empty())
        {
            quality.spatialRestackRequested = true;
            quality.spatialRestackApplied = true;
            quality.spatialRestackInitialClusterCount = 3;
            quality.spatialRestackFlaggedFrameThreshold = 0.05;
            quality.hasSpatialRejectionClusters = true;
            quality.spatialClusterCount = 1;
            quality.spatialClusterPixels = 46;
            quality.spatialLargestClusterPixels = 46;
            quality.spatialClusterFraction = 46.0 / static_cast<double>(plan.width * plan.height);
            quality.spatialLargestClusterFraction = quality.spatialClusterFraction;
            quality.spatialRejectedElements = 212;
            quality.spatialRejectedElementFraction = 212.0 / static_cast<double>(plan.width * plan.height);
            RejectionClusterRecord cluster;
            cluster.mapKind = RejectionClusterMapKind::High;
            cluster.pixelCount = 46;
            cluster.minX = 12;
            cluster.minY = 30;
            cluster.maxX = 41;
            cluster.maxY = 33;
            cluster.centroidX = 26.4;
            cluster.centroidY = 31.6;
            cluster.meanValue = 0.31;
            cluster.peakValue = 0.58;
            cluster.integratedValue = 14.3;
            quality.spatialClusters.push_back(cluster);
        }
        else if (plan.stacking.restackExcludingFlaggedFrames)
        {
            quality.spatialRestackRequested = true;
        }

        if (plan.auxiliaryMaps)
        {
            writeAuxiliaryMaps(result, plan, masterPath);
        }
        return result;
    }

    /// Writes the rejection maps and the coverage map a stack writes beside its master, under the product's names.
    void writeAuxiliaryMaps(IntegrationGroupResult& result, const LightGroupPlan& plan, const QString& masterPath)
    {
        const QFileInfo master(masterPath);
        const auto writeMap = [&](const ImageIntegration::StackingAuxiliaryOutputSpec& spec, double level) {
            const QString path = master.dir().filePath(spec.filename);
            const std::uint32_t seed = seedOf(path);
            const SampleRule pattern = [level, seed](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                return level * (0.5 + 0.5 * std::abs(scatter(seed + x, y)));
            };
            if (!writeFloatImage(path, plan.width, plan.height, FixtureChannels::Mono, level, pattern, {}))
            {
                m_usable = false;
            }
            return path;
        };
        for (const ImageIntegration::RejectionMapSpec& spec :
             ImageIntegration::rejectionMapsForOutput(master, plan.stacking.mergeRejectionMaps))
        {
            RejectionMapResult map;
            map.title = spec.title;
            map.path = writeMap(spec, 0.04);
            result.rejectionMaps.push_back(map);
            m_result.generatedRejectionMaps.append(map.path);
        }
        const ImageIntegration::StackingAuxiliaryOutputSpec coverage =
            ImageIntegration::stackingAuxiliaryOutputForOutput(master,
                                                               ImageIntegration::StackingAuxiliaryOutputKind::Coverage);
        QualityMapResult map;
        map.title = coverage.title;
        map.path = writeMap(coverage, 0.9);
        result.qualityMaps.push_back(map);
    }

    /// Writes the run's metrics as its registration stores them, replacing the folder's database.
    void writeMetricsDatabase()
    {
        const QString path = outputPath(QStringLiteral("registration_metrics.db"));
        for (const QString& stale : {path, path + QStringLiteral("-wal"), path + QStringLiteral("-shm")})
        {
            QFile::remove(stale);
        }
        m_scenario.metricsDatabasePath.clear();
        if (!m_metricsStored || m_registration.empty())
        {
            return;
        }

        an::ui::RegistrationMetricsWriter writer(
            path, QStringLiteral("ReportScenario_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QString error;
        const bool opened = writer.initialize(&error);
        EXPECT_TRUE(opened) << error.toStdString();
        if (!opened)
        {
            m_usable = false;
            return;
        }
        for (const RegistrationPlan& registration : m_registration)
        {
            an::ui::RegistrationGroupState state;
            state.groupKey = registration.key;
            state.settingsVersion = registration.settings.version;
            state.detectionHash = QStringLiteral("6186459e4ac0a7bf3f00b4ce7aea2673a4abc585");
            state.registrationHash = QStringLiteral("48bc4da049508b958bf8afbd37962ac1df93b8cd");
            state.lightFilesHash = QStringLiteral("55c59aacc816ce31a937d2b5b9e06802a0fa3fa7");
            state.updatedAt = 1774500000;
            if (!registration.metrics.empty())
            {
                state.referenceFrameId = registration.metrics.front().frameId.canonicalPath();
            }
            const bool stored = writer.upsertGroupState(state, &error) &&
                                writer.upsertMetricsForGroup(registration.key, registration.settings.version,
                                                             registration.metrics, &error);
            EXPECT_TRUE(stored) << error.toStdString();
            if (!stored)
            {
                m_usable = false;
                return;
            }
        }
        m_scenario.metricsDatabasePath = path;
    }

    /// The run's stages, in the manifest's order, with what each took; a stage the run did not reach has no result.
    void completeStages()
    {
        const bool hasLights = !m_manifest.integrationGroups.empty();
        const bool incremental = m_manifest.executionMode == ExecutionMode::Incremental;
        const bool drizzles = std::any_of(m_manifest.integrationGroups.begin(), m_manifest.integrationGroups.end(),
                                          [](const WorkflowManifest::IntegrationGroup& group) {
                                              return group.drizzle.has_value() && group.drizzle->enabled;
                                          });

        struct StagePlan
        {
            QString sequenceName;
            QString resultName;
            bool planned = false;
            bool ran = false;
            int groups = 0;
            int durationMs = 0;
        };
        const int lights = m_facts.lightsTaken - m_facts.lightsLeftOutByUser;
        const bool stacked = m_stackedGroups > 0;
        std::vector<StagePlan> stages = {
            {QStringLiteral("Bias"), QStringLiteral("BiasStacking"), m_manifest.biasStage.has_value(),
             m_madeCalibrationByType[0] > 0 || (!incremental && m_manifest.biasStage.has_value()), 0,
             m_madeCalibrationByType[0] > 0 ? 906 : 9},
            {QStringLiteral("Dark"), QStringLiteral("DarkStacking"), m_manifest.darkStage.has_value(),
             m_madeCalibrationByType[1] > 0 || (!incremental && m_manifest.darkStage.has_value()), 0,
             m_madeCalibrationByType[1] > 0 ? 636 : 2},
            {QStringLiteral("Flat"), QStringLiteral("FlatStacking"), m_manifest.flatStage.has_value(),
             m_madeCalibrationByType[2] > 0 || (!incremental && m_manifest.flatStage.has_value()), 0,
             m_madeCalibrationByType[2] > 0 ? 2750 : 4},
            {QStringLiteral("LightsRegistration"), QStringLiteral("LightsRegistration"), hasLights,
             stacked || !incremental, 0, 300 + lights * 60},
            {QStringLiteral("LightsAlignment"), QStringLiteral("LightsAlignment"), hasLights, stacked || !incremental,
             0, 280 + lights * 70},
            {QStringLiteral("LightsNormalization"), QStringLiteral("LightsNormalization"), hasLights,
             stacked || !incremental, 0, 150 + lights * 30},
            {QStringLiteral("LightsDrizzle"), QStringLiteral("LightsDrizzle"), hasLights && drizzles,
             stacked || !incremental, 0, 900 + lights * 45},
            {QStringLiteral("LightsStacking"), QStringLiteral("LightsStacking"), hasLights, stacked || !incremental,
             m_stackedGroups, 0},
        };
        for (const IntegrationGroupResult& group : m_result.integrationGroupResults)
        {
            stages.back().durationMs += group.durationMs;
        }

        // A run that stopped before any master stopped in registration; one that stopped later, in stacking.
        const bool stopped = !m_result.success;
        const QString stoppedIn =
            !stopped ? QString()
                     : (m_facts.mastersMade + m_facts.mastersFailed > 0 ? QStringLiteral("LightsStacking")
                                                                        : QStringLiteral("LightsRegistration"));
        bool reached = true;
        qint64 stageTotalMs = 0;
        for (const StagePlan& stage : stages)
        {
            if (!stage.planned)
            {
                continue;
            }
            m_manifest.stageSequence.append(stage.sequenceName);
            if (!stage.ran || !reached)
            {
                continue;
            }
            WorkflowExecutor::Result::StageTiming timing;
            timing.stageName = stage.resultName;
            timing.durationMs = stage.durationMs;
            timing.groupsProcessed = stage.groups;
            timing.status = QStringLiteral("completed");
            if (stage.resultName == stoppedIn)
            {
                timing.status = m_result.cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed");
                reached = false;
            }
            stageTotalMs += timing.durationMs;
            m_log.append(QStringLiteral("\n═══ STAGE: %1 (%2 group%3) ═══")
                             .arg(stage.resultName)
                             .arg((std::max)(1, stage.groups))
                             .arg(stage.groups == 1 ? QString() : QStringLiteral("s")));
            m_log.append(
                QStringLiteral("[INFO] %1 %2 in %3 ms").arg(stage.resultName, timing.status).arg(timing.durationMs));
            m_result.stageResults.push_back(std::move(timing));
        }

        for (const RegistrationPlan& registration : m_registration)
        {
            const bool aligned = std::any_of(m_result.stageResults.begin(), m_result.stageResults.end(),
                                             [](const WorkflowExecutor::Result::StageTiming& timing) {
                                                 return timing.stageName == QStringLiteral("LightsAlignment") &&
                                                        timing.status == QStringLiteral("completed");
                                             });
            if (!aligned)
            {
                break;
            }
            AlignmentGroupResult alignment;
            alignment.groupKey = registration.key;
            alignment.totalAligned = static_cast<int>(
                std::count_if(registration.metrics.begin(), registration.metrics.end(),
                              [](const an::ui::RegistrationMetricRecord& record) { return record.metrics.succeeded; }));
            alignment.sipCorrectedCount = registration.sipCorrected;
            alignment.caCorrectedCount = registration.caCorrected;
            alignment.caPassthroughCount = registration.caPassthrough;
            m_result.alignmentGroupResults.push_back(alignment);
        }

        m_result.preparationTiming.captured = true;
        m_result.preparationTiming.manifestBuildMs = 3;
        m_result.preparationTiming.planAndValidationMs = 34;
        m_result.executorTiming.captured = true;
        m_result.executorTiming.stageExecutionMs = stageTotalMs + 18;
        m_result.executorTiming.stateCommitMs = 5;
        m_result.executorTiming.archiveMs = 298;
        m_result.executorTiming.reportMs = 307;
        m_result.totalDurationMs = static_cast<int>(m_result.executorTiming.exclusivePhasesTotalMs());

        for (const QString& warning : m_result.warnings)
        {
            m_log.append(QStringLiteral("[WARN] %1").arg(warning));
        }
        for (const QString& error : m_result.errors)
        {
            m_log.append(QStringLiteral("[ERROR] %1").arg(error));
        }
        m_result.capturedLog = m_log.join(QLatin1Char('\n'));
    }

    ReportScenario m_scenario;
    std::shared_ptr<an::consumer::EngineSession> m_session;
    bool m_reportedWithSession = true;
    bool m_usable = true;
    bool m_lightFilesWritten = true;
    bool m_metricsStored = true;

    WorkflowManifest m_manifest;
    WorkflowExecutor::Result m_result;
    ReportScenarioFacts m_facts;
    std::vector<ReportScenarioLeftOutLight> m_leftOut;
    std::vector<RegistrationPlan> m_registration;
    std::vector<QDateTime> m_nightClocks;
    QStringList m_log;
    int m_stackedGroups = 0;
    std::array<int, 3> m_madeCalibrationByType = {0, 0, 0};
};

namespace
{
/// The lights each night gave the five filters of one target, by filter.
struct FiveFilterNights
{
    std::vector<int> luminance = {32, 28};
    std::vector<int> red = {14, 16};
    std::vector<int> green = {16, 14};
    std::vector<int> blue = {15, 15};
    std::vector<int> hydrogenAlpha = {10, 14};
};

/// What became of the five filters' masters.
struct FiveFilterOutcomes
{
    MasterOutcome luminance = MasterOutcome::Made;
    MasterOutcome red = MasterOutcome::Made;
    MasterOutcome green = MasterOutcome::Made;
    MasterOutcome blue = MasterOutcome::Made;
    MasterOutcome hydrogenAlpha = MasterOutcome::Made;
    bool combinedRgb = true;
    CalibrationOrigin calibration = CalibrationOrigin::Made;
};

/// The frame size of the camera the hydrogen-alpha lights were taken with: another camera, so another registration
/// group.
constexpr int kNarrowbandWidth = 48;
constexpr int kNarrowbandHeight = 36;

/// Adds the run of five filters of M 20: broadband lights calibrated with bias, dark and a flat per filter, and
/// hydrogen-alpha lights of another camera calibrated with a dark alone.
void addFiveFilterRun(ReportScenarioBuilder& builder, const FiveFilterNights& nights = {},
                      const FiveFilterOutcomes& outcomes = {})
{
    const QString target = QStringLiteral("M 20");
    // The bias master is the one an earlier session left in the folder; the darks hold their bias.
    const FrameId bias = builder.addBias(30, CalibrationOrigin::Reused);
    const FrameId dark = builder.addDark(120.0, 20, outcomes.calibration);
    const FrameId narrowbandDark =
        builder.addDark(300.0, 16, outcomes.calibration, kNarrowbandWidth, kNarrowbandHeight);

    const int broadband = builder.addRegistrationGroup(target);
    const int narrowband = builder.addRegistrationGroup(target, {}, kNarrowbandWidth, kNarrowbandHeight);

    struct Filter
    {
        QString name;
        const std::vector<int>* nights;
        MasterOutcome outcome;
        double seeing;
        double level;
        int nightStartMinutes;
    };
    // Each filter has its slot of the night, so a run that takes fewer lights of one filter takes the same lights
    // of the others.
    const std::array<Filter, 4> filters = {{
        {QStringLiteral("Luminance"), &nights.luminance, outcomes.luminance, 2.7, 0.14, 0},
        {QStringLiteral("Red"), &nights.red, outcomes.red, 2.9, 0.10, 75},
        {QStringLiteral("Green"), &nights.green, outcomes.green, 2.8, 0.11, 115},
        {QStringLiteral("Blue"), &nights.blue, outcomes.blue, 3.1, 0.09, 155},
    }};
    for (const Filter& filter : filters)
    {
        LightGroupPlan plan;
        plan.target = target;
        plan.filter = filter.name;
        plan.exposureSeconds = 120.0;
        plan.lightsPerNight = *filter.nights;
        plan.nightStartMinutes = filter.nightStartMinutes;
        plan.registrationGroup = broadband;
        plan.dark = dark;
        plan.flat =
            builder.addFlat(filter.name, QStringLiteral("2026-03-23"), 40, bias, std::nullopt, outcomes.calibration);
        plan.calibration.cosmeticCorrection = true;
        plan.stacking.weighting = QStringLiteral("Weighted FWHM");
        plan.stacking.createRejectionMaps = filter.name == QStringLiteral("Luminance");
        plan.stacking.createQualityMaps = plan.stacking.createRejectionMaps;
        plan.auxiliaryMaps = plan.stacking.createRejectionMaps;
        plan.outcome = filter.outcome;
        plan.solution = MasterSolutionOutcome::Propagated;
        plan.seeing = filter.seeing;
        plan.masterLevel = filter.level;
        builder.addLightGroup(plan);
    }

    LightGroupPlan hydrogenAlpha;
    hydrogenAlpha.target = target;
    hydrogenAlpha.filter = QStringLiteral("Ha");
    hydrogenAlpha.width = kNarrowbandWidth;
    hydrogenAlpha.height = kNarrowbandHeight;
    hydrogenAlpha.exposureSeconds = 300.0;
    hydrogenAlpha.lightsPerNight = nights.hydrogenAlpha;
    hydrogenAlpha.nightStartMinutes = 195;
    hydrogenAlpha.registrationGroup = narrowband;
    hydrogenAlpha.dark = narrowbandDark;
    hydrogenAlpha.calibration.cosmeticCorrection = true;
    // Its own choices, where the broadband groups share theirs.
    hydrogenAlpha.stacking.rejectionMethod = QStringLiteral("Linear Fit Clipping");
    hydrogenAlpha.stacking.sigmaLow = 4.0;
    hydrogenAlpha.stacking.sigmaHigh = 2.5;
    hydrogenAlpha.outcome = outcomes.hydrogenAlpha;
    hydrogenAlpha.seeing = 3.4;
    hydrogenAlpha.masterLevel = 0.06;
    builder.addLightGroup(hydrogenAlpha);

    if (outcomes.combinedRgb)
    {
        builder.addCombinedRgb(broadband);
    }
}

/// The notes a person wrote for the session, which a report prints near its top.
void writeSessionNotes(const ReportScenarioBuilder& builder)
{
    const QString path = builder.outputPath(QStringLiteral(".afternight/image_integration/session_notes.txt"));
    EXPECT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("Two clear nights; high cloud after 02:30 on the second.\nFocus redone before the Ha run.\n");
}

/// One mono master of one night with nothing to calibrate it with.
void addOneMonoMaster(ReportScenarioBuilder& builder)
{
    const int registration = builder.addRegistrationGroup(QStringLiteral("NGC 253"));
    LightGroupPlan plan;
    plan.target = QStringLiteral("NGC 253");
    plan.filter = QStringLiteral("Luminance");
    plan.exposureSeconds = 180.0;
    plan.lightsPerNight = {18};
    plan.registrationGroup = registration;
    builder.addLightGroup(plan);
}
} // namespace

const std::vector<ReportScenarioId>& allReportScenarios()
{
    static const std::vector<ReportScenarioId> kScenarios = {
        ReportScenarioId::OneMonoMaster,
        ReportScenarioId::OneColourMaster,
        ReportScenarioId::FiveFilters,
        ReportScenarioId::TwoTargets,
        ReportScenarioId::DrizzledGroup,
        ReportScenarioId::CometRun,
        ReportScenarioId::HdrExposureMerge,
        ReportScenarioId::FailedInStacking,
        ReportScenarioId::CancelledAfterTwoMasters,
        ReportScenarioId::CancelledBeforeAnyMaster,
        ReportScenarioId::FailedBeforeAnyMaster,
        ReportScenarioId::IncrementalRun,
        ReportScenarioId::EveryGroupReused,
        ReportScenarioId::PreCalibratedAndSuppliedDark,
        ReportScenarioId::FiveFiltersWithoutSession,
        ReportScenarioId::FiveFiltersWithoutMetrics,
        ReportScenarioId::FiveFiltersAfterEarlierRun,
        ReportScenarioId::LiteFrontEnd,
        ReportScenarioId::FiveFiltersWithoutPayloads,
        ReportScenarioId::TwoThousandLights,
        ReportScenarioId::EveryLightState,
        ReportScenarioId::PhotometricAndLocalWeighting,
        ReportScenarioId::DistortionCorrectionsAndManualPoints,
    };
    return kScenarios;
}

QString reportScenarioName(ReportScenarioId id)
{
    switch (id)
    {
    case ReportScenarioId::OneMonoMaster:
        return QStringLiteral("one-mono-master");
    case ReportScenarioId::OneColourMaster:
        return QStringLiteral("one-colour-master");
    case ReportScenarioId::FiveFilters:
        return QStringLiteral("five-filters");
    case ReportScenarioId::TwoTargets:
        return QStringLiteral("two-targets");
    case ReportScenarioId::DrizzledGroup:
        return QStringLiteral("drizzled-group");
    case ReportScenarioId::CometRun:
        return QStringLiteral("comet-run");
    case ReportScenarioId::HdrExposureMerge:
        return QStringLiteral("hdr-exposure-merge");
    case ReportScenarioId::FailedInStacking:
        return QStringLiteral("failed-in-stacking");
    case ReportScenarioId::CancelledAfterTwoMasters:
        return QStringLiteral("cancelled-after-two-masters");
    case ReportScenarioId::CancelledBeforeAnyMaster:
        return QStringLiteral("cancelled-before-any-master");
    case ReportScenarioId::FailedBeforeAnyMaster:
        return QStringLiteral("failed-before-any-master");
    case ReportScenarioId::IncrementalRun:
        return QStringLiteral("incremental-run");
    case ReportScenarioId::EveryGroupReused:
        return QStringLiteral("every-group-reused");
    case ReportScenarioId::PreCalibratedAndSuppliedDark:
        return QStringLiteral("pre-calibrated-and-supplied-dark");
    case ReportScenarioId::FiveFiltersWithoutSession:
        return QStringLiteral("five-filters-without-session");
    case ReportScenarioId::FiveFiltersWithoutMetrics:
        return QStringLiteral("five-filters-without-metrics");
    case ReportScenarioId::FiveFiltersAfterEarlierRun:
        return QStringLiteral("five-filters-after-earlier-run");
    case ReportScenarioId::LiteFrontEnd:
        return QStringLiteral("lite-front-end");
    case ReportScenarioId::FiveFiltersWithoutPayloads:
        return QStringLiteral("five-filters-without-payloads");
    case ReportScenarioId::TwoThousandLights:
        return QStringLiteral("two-thousand-lights");
    case ReportScenarioId::EveryLightState:
        return QStringLiteral("every-light-state");
    case ReportScenarioId::PhotometricAndLocalWeighting:
        return QStringLiteral("photometric-and-local-weighting");
    case ReportScenarioId::DistortionCorrectionsAndManualPoints:
        return QStringLiteral("distortion-corrections-and-manual-points");
    }
    return QStringLiteral("unknown");
}

std::optional<ReportScenario> buildReportScenario(ReportScenarioId id)
{
    const QString runId = QStringLiteral("20260326_071204_118");
    const QString earlierRunId = QStringLiteral("20260324_081530_402");

    ReportScenarioBuilder builder(id, id != ReportScenarioId::FiveFiltersWithoutSession);
    if (!builder.usable())
    {
        return std::nullopt;
    }

    switch (id)
    {
    case ReportScenarioId::OneMonoMaster:
        builder.beginRun(runId);
        addOneMonoMaster(builder);
        break;

    case ReportScenarioId::LiteFrontEnd:
        builder.beginRun(runId);
        builder.manifest().processLabel = QStringLiteral("Image Integration Lite");
        addOneMonoMaster(builder);
        break;

    case ReportScenarioId::OneColourMaster: {
        builder.beginRun(runId);
        const FrameId bias = builder.addBias(30);
        const FrameId dark = builder.addDark(180.0, 20);
        const FrameId flat = builder.addFlat(QString(), QStringLiteral("2026-03-23"), 40, bias, std::nullopt);
        RegistrationSettings settings;
        settings.cfaPattern = QStringLiteral("RGGB");
        settings.debayerMethod = QStringLiteral("RCD");
        const int registration = builder.addRegistrationGroup(QStringLiteral("NGC 253"), settings);
        LightGroupPlan plan;
        plan.target = QStringLiteral("NGC 253");
        plan.exposureSeconds = 180.0;
        plan.lightsPerNight = {17, 18};
        plan.registrationGroup = registration;
        plan.colour = true;
        plan.dark = dark;
        plan.flat = flat;
        plan.calibration.cosmeticCorrection = true;
        plan.solution = MasterSolutionOutcome::Solved;
        builder.addLightGroup(plan);
        break;
    }

    case ReportScenarioId::FiveFilters:
    case ReportScenarioId::FiveFiltersWithoutSession:
        builder.beginRun(runId);
        addFiveFilterRun(builder);
        writeSessionNotes(builder);
        break;

    case ReportScenarioId::FiveFiltersWithoutMetrics:
        builder.beginRun(runId);
        builder.setMetricsStored(false);
        addFiveFilterRun(builder);
        break;

    case ReportScenarioId::FiveFiltersWithoutPayloads:
        builder.beginRun(runId);
        addFiveFilterRun(builder);
        // Room for a run's manifest, report and log, and for none of its masters.
        builder.scenario().historyByteLimit = 600 * 1024;
        break;

    case ReportScenarioId::FiveFiltersAfterEarlierRun: {
        // The first night alone, run and archived two days before.
        builder.beginRun(earlierRunId);
        FiveFilterNights firstNight;
        firstNight.luminance = {32};
        firstNight.red = {14};
        firstNight.green = {16};
        firstNight.blue = {15};
        firstNight.hydrogenAlpha = {10};
        addFiveFilterRun(builder, firstNight);
        if (!builder.archiveAsEarlierRun())
        {
            return std::nullopt;
        }
        builder.beginRun(runId);
        addFiveFilterRun(builder);
        break;
    }

    case ReportScenarioId::IncrementalRun: {
        // A full run with the first night's luminance and hydrogen-alpha, then a run that adds the second night's:
        // the red, green and blue masters are still valid.
        builder.beginRun(earlierRunId);
        FiveFilterNights earlier;
        earlier.luminance = {32};
        earlier.hydrogenAlpha = {10};
        addFiveFilterRun(builder, earlier);
        if (!builder.archiveAsEarlierRun())
        {
            return std::nullopt;
        }
        builder.beginRun(runId, ExecutionMode::Incremental);
        FiveFilterOutcomes outcomes;
        outcomes.red = MasterOutcome::Reused;
        outcomes.green = MasterOutcome::Reused;
        outcomes.blue = MasterOutcome::Reused;
        outcomes.combinedRgb = false;
        outcomes.calibration = CalibrationOrigin::Reused;
        addFiveFilterRun(builder, {}, outcomes);
        break;
    }

    case ReportScenarioId::EveryGroupReused: {
        builder.beginRun(earlierRunId);
        addFiveFilterRun(builder);
        if (!builder.archiveAsEarlierRun())
        {
            return std::nullopt;
        }
        builder.beginRun(runId, ExecutionMode::Incremental);
        FiveFilterOutcomes outcomes;
        outcomes.luminance = MasterOutcome::Reused;
        outcomes.red = MasterOutcome::Reused;
        outcomes.green = MasterOutcome::Reused;
        outcomes.blue = MasterOutcome::Reused;
        outcomes.hydrogenAlpha = MasterOutcome::Reused;
        outcomes.combinedRgb = false;
        outcomes.calibration = CalibrationOrigin::Reused;
        addFiveFilterRun(builder, {}, outcomes);
        break;
    }

    case ReportScenarioId::TwoTargets: {
        builder.beginRun(runId);
        const FrameId dark = builder.addDark(120.0, 20);
        for (const QString& target : {QStringLiteral("M 83"), QStringLiteral("NGC 5128")})
        {
            const int registration = builder.addRegistrationGroup(target);
            for (const QString& filter : {QStringLiteral("Luminance"), QStringLiteral("Ha")})
            {
                LightGroupPlan plan;
                plan.target = target;
                plan.filter = filter;
                plan.lightsPerNight =
                    target == QStringLiteral("M 83") ? std::vector<int>{12, 10} : std::vector<int>{8, 9};
                plan.registrationGroup = registration;
                plan.dark = dark;
                plan.seeing = filter == QStringLiteral("Ha") ? 3.3 : 2.8;
                builder.addLightGroup(plan);
            }
        }
        break;
    }

    case ReportScenarioId::DrizzledGroup: {
        builder.beginRun(runId);
        const int registration = builder.addRegistrationGroup(QStringLiteral("M 83"));
        LightGroupPlan drizzled;
        drizzled.target = QStringLiteral("M 83");
        drizzled.filter = QStringLiteral("Luminance");
        drizzled.lightsPerNight = {24};
        drizzled.registrationGroup = registration;
        drizzled.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
        drizzled.drizzle->enabled = true;
        drizzled.drizzle->scale = 2.0;
        drizzled.drizzle->pixelFraction = 0.8;
        drizzled.stacking.drizzle.enabled = true;
        drizzled.stacking.drizzle.pixelFraction = 0.8;
        builder.addLightGroup(drizzled);

        LightGroupPlan plain;
        plain.target = QStringLiteral("M 83");
        plain.filter = QStringLiteral("Ha");
        plain.exposureSeconds = 300.0;
        plain.lightsPerNight = {12};
        plain.registrationGroup = registration;
        builder.addLightGroup(plain);
        break;
    }

    case ReportScenarioId::CometRun: {
        builder.beginRun(runId);
        RegistrationSettings settings;
        settings.registrationMethod = QStringLiteral("comet_tracking");
        settings.cometTracking = RegistrationSettings::CometTrackingSettings{};
        const int registration = builder.addRegistrationGroup(QStringLiteral("C2023 A3"), settings);
        LightGroupPlan plan;
        plan.target = QStringLiteral("C2023 A3");
        plan.filter = QStringLiteral("Luminance");
        plan.exposureSeconds = 60.0;
        plan.lightsPerNight = {30};
        plan.registrationGroup = registration;
        plan.stacking.cometStackingMode = QStringLiteral("Comet");
        builder.addLightGroup(plan);
        break;
    }

    case ReportScenarioId::HdrExposureMerge: {
        builder.beginRun(runId);
        const int registration = builder.addRegistrationGroup(QStringLiteral("M 42"));
        LightGroupPlan plan;
        plan.target = QStringLiteral("M 42");
        plan.filter = QStringLiteral("Luminance");
        plan.exposureCycle = {5.0, 30.0, 180.0};
        plan.lightsPerNight = {27};
        plan.registrationGroup = registration;
        plan.stacking.hdr.enabled = true;
        builder.addLightGroup(plan);
        break;
    }

    case ReportScenarioId::FailedInStacking: {
        builder.beginRun(runId);
        FiveFilterOutcomes outcomes;
        outcomes.blue = MasterOutcome::Failed;
        outcomes.hydrogenAlpha = MasterOutcome::NotReached;
        outcomes.combinedRgb = false;
        addFiveFilterRun(builder, {}, outcomes);
        builder.setStopped(false, QStringLiteral("Lights stacking failed: 1 group could not be stacked."));
        break;
    }

    case ReportScenarioId::CancelledAfterTwoMasters: {
        builder.beginRun(runId);
        FiveFilterOutcomes outcomes;
        outcomes.green = MasterOutcome::NotReached;
        outcomes.blue = MasterOutcome::NotReached;
        outcomes.hydrogenAlpha = MasterOutcome::NotReached;
        outcomes.combinedRgb = false;
        addFiveFilterRun(builder, {}, outcomes);
        builder.setStopped(true, QStringLiteral("Cancelled"));
        break;
    }

    case ReportScenarioId::CancelledBeforeAnyMaster:
    case ReportScenarioId::FailedBeforeAnyMaster: {
        builder.beginRun(runId);
        FiveFilterOutcomes outcomes;
        outcomes.luminance = MasterOutcome::NotReached;
        outcomes.red = MasterOutcome::NotReached;
        outcomes.green = MasterOutcome::NotReached;
        outcomes.blue = MasterOutcome::NotReached;
        outcomes.hydrogenAlpha = MasterOutcome::NotReached;
        outcomes.combinedRgb = false;
        addFiveFilterRun(builder, {}, outcomes);
        const bool cancelled = id == ReportScenarioId::CancelledBeforeAnyMaster;
        if (!cancelled)
        {
            builder.result().errors.append(
                QStringLiteral("Registration failed for group 'reg|64x48|M 20': no reference frame could be chosen."));
        }
        builder.setStopped(cancelled,
                           cancelled ? QStringLiteral("Cancelled") : QStringLiteral("Lights registration failed."));
        builder.scenario().archivedByExecutor = false;
        break;
    }

    case ReportScenarioId::PreCalibratedAndSuppliedDark: {
        builder.beginRun(runId);
        const FrameId dark = builder.addDark(120.0, 1, CalibrationOrigin::Supplied);
        const int registration = builder.addRegistrationGroup(QStringLiteral("M 83"));
        LightGroupPlan calibrated;
        calibrated.target = QStringLiteral("M 83");
        calibrated.filter = QStringLiteral("Luminance");
        calibrated.lightsPerNight = {16};
        calibrated.registrationGroup = registration;
        calibrated.dark = dark;
        builder.addLightGroup(calibrated);

        LightGroupPlan preCalibrated;
        preCalibrated.target = QStringLiteral("M 83");
        preCalibrated.filter = QStringLiteral("Ha");
        preCalibrated.exposureSeconds = 300.0;
        preCalibrated.lightsPerNight = {12};
        preCalibrated.registrationGroup = registration;
        preCalibrated.preCalibrated = true;
        builder.addLightGroup(preCalibrated);
        break;
    }

    case ReportScenarioId::TwoThousandLights: {
        builder.beginRun(runId);
        builder.setLightFilesWritten(false);
        const int registration = builder.addRegistrationGroup(QStringLiteral("Jupiter"));
        LightGroupPlan plan;
        plan.target = QStringLiteral("Jupiter");
        plan.filter = QStringLiteral("Luminance");
        plan.exposureSeconds = 2.0;
        plan.lightsPerNight = {2000};
        plan.registrationGroup = registration;
        plan.registrationFailures = 40;
        plan.stacking.weighting = QStringLiteral("Weighted FWHM");
        builder.addLightGroup(plan);
        break;
    }

    case ReportScenarioId::EveryLightState: {
        builder.beginRun(runId);
        const int registration = builder.addRegistrationGroup(QStringLiteral("M 8"));
        LightGroupPlan luminance;
        luminance.target = QStringLiteral("M 8");
        luminance.filter = QStringLiteral("Luminance");
        luminance.lightsPerNight = {20, 20};
        luminance.registrationGroup = registration;
        luminance.excludedByUser = 3;
        luminance.stackExcludedByUser = 2;
        luminance.registrationFailures = 4;
        luminance.droppedByStack = 1;
        luminance.excludedOnRestack = 2;
        luminance.stacking.spatialRejectionClustering = true;
        luminance.stacking.restackExcludingFlaggedFrames = true;
        luminance.stacking.weighting = QStringLiteral("Weighted FWHM");
        builder.addLightGroup(luminance);

        // Every light of this group was excluded: the run has no group for it.
        LightGroupPlan leftOut;
        leftOut.target = QStringLiteral("M 8");
        leftOut.filter = QStringLiteral("Oiii");
        leftOut.exposureSeconds = 300.0;
        leftOut.lightsPerNight = {6};
        leftOut.registrationGroup = registration;
        leftOut.excludedByUser = 6;
        builder.addLightGroup(leftOut);
        break;
    }

    case ReportScenarioId::PhotometricAndLocalWeighting: {
        builder.beginRun(runId);
        const int registration = builder.addRegistrationGroup(QStringLiteral("M 83"));
        LightGroupPlan photometric;
        photometric.target = QStringLiteral("M 83");
        photometric.filter = QStringLiteral("Luminance");
        photometric.lightsPerNight = {14, 12};
        photometric.registrationGroup = registration;
        photometric.photometry = true;
        photometric.stacking.weighting = QStringLiteral("Photometric");
        photometric.solution = MasterSolutionOutcome::Propagated;
        builder.addLightGroup(photometric);

        LightGroupPlan local;
        local.target = QStringLiteral("M 83");
        local.filter = QStringLiteral("Ha");
        local.exposureSeconds = 300.0;
        local.lightsPerNight = {10, 8};
        local.registrationGroup = registration;
        local.photometry = true;
        local.stacking.weighting = QStringLiteral("Noise");
        local.stacking.localWeighting.enabled = true;
        local.solution = MasterSolutionOutcome::Propagated;
        builder.addLightGroup(local);
        break;
    }

    case ReportScenarioId::DistortionCorrectionsAndManualPoints: {
        builder.beginRun(runId);
        RegistrationSettings corrected;
        corrected.autoSolveUnsolvedFrames = true;
        corrected.chromaticAberration.enabled = true;
        const int correctedGroup = builder.addRegistrationGroup(QStringLiteral("M 31"), corrected);
        LightGroupPlan colour;
        colour.target = QStringLiteral("M 31");
        colour.exposureSeconds = 180.0;
        colour.lightsPerNight = {20};
        colour.registrationGroup = correctedGroup;
        colour.colour = true;
        colour.photometry = true;
        colour.solution = MasterSolutionOutcome::Propagated;
        builder.addLightGroup(colour);
        builder.setAlignmentCorrections(correctedGroup, 20, 17, 3);

        RegistrationSettings manual;
        manual.registrationMethod = QStringLiteral("manual_points");
        RegistrationSettings::ManualControlPointList points;
        points.append({QPointF(12.5, 9.5), QPointF(14.0, 10.25)});
        points.append({QPointF(48.5, 11.5), QPointF(50.0, 12.0)});
        points.append({QPointF(30.5, 40.5), QPointF(31.75, 41.5)});
        const int manualGroup = builder.addRegistrationGroup(QStringLiteral("Moon"), manual);
        LightGroupPlan moon;
        moon.target = QStringLiteral("Moon");
        moon.filter = QStringLiteral("Luminance");
        moon.exposureSeconds = 1.0;
        moon.lightsPerNight = {12};
        moon.registrationGroup = manualGroup;
        builder.addLightGroup(moon);
        // The points are stated per light, by the light's path.
        WorkflowManifest::RegistrationGroup& group =
            builder.manifest().registrationGroups[static_cast<std::size_t>(manualGroup)];
        for (std::size_t light = 1; light < group.lightFiles.size(); ++light)
        {
            group.settings.manualControlPointsByFrame.insert(group.lightFiles[light].canonicalPath(), points);
        }
        break;
    }
    }

    return builder.finish();
}

bool archiveScenarioRun(ReportScenario& scenario)
{
    const std::unique_ptr<WorkflowArchiver> archiver =
        scenario.historyByteLimit > 0
            ? std::make_unique<WorkflowArchiver>(
                  scenario.outputDirectory, ImageIntegration::Config::kRunHistoryMaxEntries, scenario.historyByteLimit)
            : std::make_unique<WorkflowArchiver>(scenario.outputDirectory);
    archiver->setEngineSession(scenario.session());
    const WorkflowArchiver::ArchiveResult archived = archiver->commit(scenario.manifest, scenario.result);
    EXPECT_TRUE(archived.success) << archived.error.toStdString();
    if (!archived.success)
    {
        return false;
    }
    EXPECT_EQ(QDir::cleanPath(archived.archivePath), QDir::cleanPath(scenario.runDirectory));
    return true;
}

} // namespace an::tests::integration::report
