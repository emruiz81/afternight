// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameQualityDashboard.h"
#include "app/UiStyleTokens.h"
#include <AfterNightPlatform/Logging.h>
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"
#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/ImageIntegration/core/StackWeightingInputs.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/tabs/TabStyling.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QCoreApplication>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSignalBlocker>
#include <QStyleOption>
#include <QTabWidget>
#include <QTimer>
#include <QStringList>
#include <QTableWidget>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "processes/ImageIntegration/core/MathUtils.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "theme/AnDesignTokens.h"
#include "processes/ImageIntegration/widgets/FrameQualityDashboardColumns.h"
#include "processes/ImageIntegration/widgets/NumericSortItem.h"
#include "processes/ImageIntegration/widgets/WeightHistogramWidget.h"

using ImageIntegration::clamp01;

namespace
{
[[nodiscard]] bool equalsCaseInsensitive(const QString& a, const QString& b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

void setEmptyMessage(QTableWidget* table, const QString& message)
{
    if (!table)
    {
        return;
    }

    table->clearContents();
    table->clearSpans();
    table->setRowCount(1);
    table->setSpan(0, 0, 1, table->columnCount());
    auto* item = new QTableWidgetItem(message);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(QBrush(table->palette().color(QPalette::PlaceholderText)));
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    table->setItem(0, 0, item);
}

struct DbMetricRow
{
    FrameQualityMetrics metrics;
};

[[nodiscard]] bool hasRowsForVersion(QSqlDatabase& db, const QString& groupKey, int version)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM registration_metrics WHERE group_key = :group_key AND settings_version = :v LIMIT 1"));
    q.bindValue(QStringLiteral(":group_key"), groupKey);
    q.bindValue(QStringLiteral(":v"), version);
    return q.exec() && q.next();
}

[[nodiscard]] int resolveEffectiveVersion(QSqlDatabase& db, const QString& groupKey, int desiredVersion)
{
    // Check if desired_version has rows (including version 0)
    if (hasRowsForVersion(db, groupKey, desiredVersion))
    {
        return desiredVersion;
    }

    // Fallback to latest version in DB
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT MAX(settings_version) FROM registration_metrics WHERE group_key = :group_key"));
    q.bindValue(QStringLiteral(":group_key"), groupKey);
    if (q.exec() && q.next())
    {
        const int latest = q.value(0).toInt();
        if (hasRowsForVersion(db, groupKey, latest))
        {
            return latest;
        }
    }

    return -1; // Return -1 to indicate no valid version found
}

/// Opens the output directory's registration records and calls `visit` once for each registration group the frames
/// belong to, with the settings version whose rows are read: the version the group's settings name, or the latest the
/// records hold when that one has none. Does nothing when the records do not exist or cannot be opened.
void visitRegistrationMetricGroups(IntegrationDataModel* model, const std::vector<FrameInfo>& frames,
                                   const std::function<void(QSqlDatabase&, const QString&, int)>& visit)
{
    if (!model || frames.empty())
    {
        return;
    }

    const QString outputDir = model->outputDirectory().trimmed();
    if (outputDir.isEmpty())
    {
        return;
    }

    const QString dbPath = QDir(outputDir).filePath(QStringLiteral("registration_metrics.db"));
    if (!QFileInfo::exists(dbPath))
    {
        return;
    }

    const QString connName =
        QStringLiteral("FrameQualityDashboard_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    struct DbConnGuard
    {
        QString name;
        ~DbConnGuard()
        {
            if (!name.isEmpty())
            {
                QSqlDatabase::removeDatabase(name);
            }
        }
    };

    DbConnGuard guard{connName};

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(dbPath);
    if (!db.open())
    {
        db.close();
        return;
    }

    std::unordered_map<QString, int> desired_versions;
    desired_versions.reserve(32);

    for (const auto& frame : frames)
    {
        if (frame.excluded)
        {
            continue;
        }

        const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(frame));
        const QString group_key = machine_key.toString();
        if (!desired_versions.contains(group_key))
        {
            int desired = 0;
            if (auto it = model->registrationSettings().find(machine_key); it != model->registrationSettings().end())
            {
                desired = it->second.version;
            }
            desired_versions.emplace(group_key, desired);
        }
    }

    for (const auto& [group_key, desired] : desired_versions)
    {
        const int effective = resolveEffectiveVersion(db, group_key, desired);
        if (effective < 0)
        {
            continue;
        }
        visit(db, group_key, effective);
    }

    db.close();
}

[[nodiscard]] std::unordered_map<QString, DbMetricRow> loadMetricsFromDb(IntegrationDataModel* model,
                                                                         const std::vector<FrameInfo>& frames)
{
    std::unordered_map<QString, DbMetricRow> out;
    out.reserve(frames.size());
    visitRegistrationMetricGroups(
        model, frames, [&out](QSqlDatabase& db, const QString& group_key, const int effective) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT frame_id, stars, fwhm, snr, eccentricity, succeeded, "
                "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, astrometric_rms_arcsec, "
                "limiting_magnitude, catalog_matched_stars "
                "FROM registration_metrics WHERE group_key = :group_key AND settings_version = :v"));
            q.bindValue(QStringLiteral(":group_key"), group_key);
            q.bindValue(QStringLiteral(":v"), effective);
            if (!q.exec())
            {
                return;
            }

            while (q.next())
            {
                const QString frame_id = q.value(0).toString();
                DbMetricRow row;
                row.metrics.stars = q.value(1).toInt();
                row.metrics.fwhm = q.value(2).toDouble();
                row.metrics.snr = q.value(3).toDouble();
                row.metrics.eccentricity = q.value(4).toDouble();
                row.metrics.succeeded = (q.value(5).toInt() != 0);
                row.metrics.photometric_zero_point = q.value(6).toDouble();
                row.metrics.zero_point_scatter = q.value(7).toDouble();
                row.metrics.astrometric_rms_pixels = q.value(8).toDouble();
                row.metrics.astrometric_rms_arcsec = q.value(9).toDouble();
                row.metrics.limiting_magnitude = q.value(10).toDouble();
                row.metrics.catalog_matched_stars = q.value(11).toInt();
                out.emplace(frame_id, row);
            }
        });
    return out;
}

/// Each frame's background noise and normalization scale as the registration recorded them, by canonical path: the
/// figures the stack's noise weighting reads. A record that holds none gives none, and a frame whose registration
/// failed is left out, as the stack leaves it out.
[[nodiscard]] std::unordered_map<QString, FrameNoiseStatistics>
loadNoiseStatisticsFromDb(IntegrationDataModel* model, const std::vector<FrameInfo>& frames)
{
    std::unordered_map<QString, FrameNoiseStatistics> out;
    out.reserve(frames.size());
    visitRegistrationMetricGroups(
        model, frames, [&out](QSqlDatabase& db, const QString& group_key, const int effective) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT frame_id, background_noise, global_mad, global_scale "
                                     "FROM registration_metrics WHERE group_key = :group_key AND settings_version = :v "
                                     "AND succeeded = 1"));
            q.bindValue(QStringLiteral(":group_key"), group_key);
            q.bindValue(QStringLiteral(":v"), effective);
            if (!q.exec())
            {
                return;
            }

            while (q.next())
            {
                FrameNoiseStatistics noise;
                noise.background_noise = q.value(1).toDouble();
                noise.global_mad = q.value(2).toDouble();
                noise.global_scale = q.value(3).toDouble();
                out.insert_or_assign(q.value(0).toString(), noise);
            }
        });
    return out;
}

/// How long a weights preview waits before asking the session again when it had no thread free.
constexpr int kWeightPreviewRetryMilliseconds = 250;

/// What the stack's own weighting gives a preview's frames.
struct StackWeightPreview
{
    /// One weight per measurement, in their order, with mean one; empty when none were computed.
    std::vector<double> weights;

    /// Whether the session had no thread free now, so the preview is to be asked for again.
    bool busy = false;

    /// Why the weights are uniform or were not computed, for the person; empty when they were computed.
    QString note;
};

/// The weights the stack derives for the frames' measurements under the group's weighting, against the frame at
/// `referenceIndex` - the engine's own weighting, the call the stacking stage's stack makes (`deriveStackWeights`) - in
/// an operation of the process's session. The GUI thread never waits: when the session has no thread free now the
/// preview says so, and the caller asks again.
[[nodiscard]] StackWeightPreview previewStackWeights(const an::StackWeightingRequest& weighting,
                                                     const std::vector<an::StackFrameMeasurement>& measurements,
                                                     const std::uint32_t referenceIndex)
{
    StackWeightPreview preview;
    const an::consumer::EngineSession* session = ImageIntegration::detail::applicationEngineSession();
    if (session == nullptr || measurements.empty())
    {
        preview.note = QCoreApplication::translate("FrameQualityDashboard",
                                                   "The weights preview needs the application's engine session.");
        return preview;
    }

    an::Result<an::consumer::OperationScope> scope = session->tryAdmit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        preview.busy = scope.status().code() == an::ErrorCode::Busy;
        preview.note =
            QCoreApplication::translate("FrameQualityDashboard", "The weights preview is waiting for the engine: %1")
                .arg(QString::fromUtf8(scope.status().message()));
        return preview;
    }

    an::Result<ImageIntegration::DerivedStackWeights> derived =
        ImageIntegration::deriveStackWeights(scope->execution(), weighting, measurements, referenceIndex);
    if (!derived.hasValue())
    {
        preview.note =
            QCoreApplication::translate("FrameQualityDashboard", "The weights preview could not be computed: %1")
                .arg(QString::fromUtf8(derived.status().message()));
        return preview;
    }

    switch (derived->result.disposition)
    {
    case an::StackFrameWeightingDisposition::UniformNoMeasurements:
        preview.note = QCoreApplication::translate("FrameQualityDashboard",
                                                   "No frame carries the measurement this weighting reads, so the "
                                                   "stack weighs every frame equally.");
        break;
    case an::StackFrameWeightingDisposition::UniformEqualMeasurements:
        preview.note = QCoreApplication::translate("FrameQualityDashboard",
                                                   "Every frame carries the same measurement, so the stack weighs "
                                                   "every frame equally.");
        break;
    case an::StackFrameWeightingDisposition::UniformDegenerate:
        preview.note = QCoreApplication::translate("FrameQualityDashboard",
                                                   "The photometric model produced no usable weight, so the stack "
                                                   "weighs every frame equally.");
        break;
    default:
        break;
    }
    preview.weights = std::move(derived->weights);
    return preview;
}

/// Whether a weighting mode's preview is the stack's own weights, normalized to mean one: every weighting's is.
[[nodiscard]] bool usesStackWeights(const QString& mode)
{
    return !mode.trimmed().isEmpty() && !equalsCaseInsensitive(mode, QStringLiteral("None"));
}

/// The figure a weighting reads of a frame, as the preview names what a frame lacks.
[[nodiscard]] QString weightingFigureName(const an::StackWeighting weighting)
{
    switch (weighting)
    {
    case an::StackWeighting::Noise:
        return QCoreApplication::translate("FrameQualityDashboard", "a background noise measurement");
    case an::StackWeighting::StarCount:
        return QCoreApplication::translate("FrameQualityDashboard", "a star count");
    case an::StackWeighting::Fwhm:
        return QCoreApplication::translate("FrameQualityDashboard", "a star width");
    case an::StackWeighting::Photometric:
        return QCoreApplication::translate("FrameQualityDashboard", "a photometric zero point");
    default:
        break;
    }
    return QCoreApplication::translate("FrameQualityDashboard", "the figure the weighting reads");
}

/// How many frames each light combines, by canonical path, as its header states it - read the way the stacking stage
/// reads it, a light whose header cannot be read counting once - or why the headers could not be read at all.
struct CombinedFrameCounts
{
    QHash<QString, std::uint32_t> counts;
    QString failure;
};

/// Reads the lights' headers in an operation of the session. Runs on the thread pool, where waiting to be admitted
/// holds no window.
[[nodiscard]] CombinedFrameCounts readCombinedFrameCounts(const an::consumer::EngineSession* session,
                                                          const QStringList& paths)
{
    CombinedFrameCounts read;
    an::Result<an::consumer::OperationScope> scope = session->admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        read.failure = QString::fromUtf8(scope.status().message());
        return read;
    }
    for (const QString& path : paths)
    {
        const std::string utf8Path = path.toUtf8().toStdString();
        const an::Result<an::CodecImageInfo> probed = an::probeImage(
            scope->engine(), scope->execution(), an::CodecReadRequest{.path = utf8Path, .validatePayload = false});
        read.counts.insert(
            path, probed.hasValue() ? ImageIntegration::combinedFrameCount(probed->metadata.codec().cards()) : 1u);
    }
    return read;
}
} // namespace

void FrameQualityDashboard::calculateWeights()
{
    if (!m_model || !m_table)
    {
        return;
    }

    m_computedWeights.clear();
    m_lastEffectiveExposureSeconds = 0.0;

    const auto key_opt = m_model->selectedRegistrationGroupKey();
    const bool filterByGroup = m_filterByRegistrationGroup;
    const GroupKey selected_key = (filterByGroup && key_opt.has_value()) ? *key_opt : GroupKey{};
    if (filterByGroup && !key_opt.has_value())
    {
        updateEffectiveExposureLabel();
        auto* hist = static_cast<WeightHistogramWidget*>(m_weightHistogram);
        hist->setMeanNormalizedSemantics(false);
        hist->setWeights({});
        return;
    }
    const auto frames = m_model->lightFrames().frames();

    struct WeightRow
    {
        QString id;
        double exposure = 0.0;
        FrameQualityMetrics metrics;
    };

    QVector<WeightRow> rows;
    rows.reserve(static_cast<int>(frames.size()));
    std::vector<FrameInfo> rowFrames;
    rowFrames.reserve(frames.size());

    for (const auto& frame : frames)
    {
        if (filterByGroup)
        {
            const GroupKey machine_key =
                RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(frame));
            if (machine_key.toString() != selected_key.toString())
            {
                continue;
            }
        }
        if (m_frameIdFilterActive && !m_frameIdFilter.contains(frame.id.canonicalPath()))
        {
            continue;
        }
        if (isFrameExcludedFromCurrentStack(frame))
        {
            continue;
        }

        auto metrics = m_model->frameQualityMetrics(frame.id);
        if (!metrics || !metrics->succeeded)
        {
            continue;
        }

        rows.push_back(WeightRow{frame.id.canonicalPath(), frame.metadata.exposureSeconds(), *metrics});
        rowFrames.push_back(frame);
    }

    if (rows.isEmpty())
    {
        updateEffectiveExposureLabel();
        auto* hist = static_cast<WeightHistogramWidget*>(m_weightHistogram);
        hist->setMeanNormalizedSemantics(false);
        hist->setWeights({});
        return;
    }

    const QString mode = m_weightingConfig.weightingMode.trimmed();
    // Every weighting's preview is the stack's own weights, mean one, which the engine derives from the measurements
    // the stack reads; uniform, with the banner saying why, wherever the stack would weigh the frames equally.
    const bool stackWeights = usesStackWeights(mode);
    const double minFloor = (std::max)(0.0, m_weightingConfig.minWeightFloor);
    auto* hist = static_cast<WeightHistogramWidget*>(m_weightHistogram);

    QVector<double> raw(rows.size(), stackWeights ? 1.0 : 0.0);
    bool weightsPreviewBusy = false;
    QString bannerText;
    if (stackWeights)
    {
        // The settings the stack reads its weighting from, through the closed choice maps.
        StackingSettings settings;
        settings.weighting = mode;
        if (!m_weightingConfig.starCountScaling.trimmed().isEmpty())
        {
            settings.starCountScaling = m_weightingConfig.starCountScaling;
        }
        settings.exponentialAlpha = m_weightingConfig.exponentialAlpha;
        settings.minWeightFloor = m_weightingConfig.minWeightFloor;
        const an::Result<ImageIntegration::IntegrationStackChoice> choice =
            ImageIntegration::integrationStackChoice(settings);
        if (!choice.hasValue())
        {
            bannerText = tr("The stack refuses this weighting, so the preview weighs every frame equally: %1")
                             .arg(QString::fromUtf8(choice.status().message()));
        }
        else
        {
            const an::StackWeighting source = choice->weighting;
            std::vector<FrameId> frameIds;
            frameIds.reserve(static_cast<std::size_t>(rows.size()));
            std::vector<an::StackFrameMeasurement> measurements(static_cast<std::size_t>(rows.size()));
            // Why the frames are weighed equally before the engine is asked, as the stack would weigh them.
            QString uniformReason;
            if (source == an::StackWeighting::ImageCount)
            {
                // Each light's own header, read on the thread pool; the preview is computed again when it lands.
                QStringList unread;
                bool everyLightOnce = true;
                for (int i = 0; i < rows.size(); ++i)
                {
                    frameIds.emplace_back(rows[i].id);
                    const auto count = m_combinedFrameCounts.constFind(rows[i].id);
                    if (count == m_combinedFrameCounts.constEnd())
                    {
                        unread.push_back(rows[i].id);
                        continue;
                    }
                    measurements[static_cast<std::size_t>(i)].integratedFrameCount = *count;
                    everyLightOnce = everyLightOnce && *count == 1u;
                }
                if (!unread.isEmpty() && m_combinedFrameCountsFailure.isEmpty())
                {
                    readCombinedFrameCountsOf(unread);
                }
                if (!m_combinedFrameCountsFailure.isEmpty())
                {
                    uniformReason = tr("The lights' headers could not be read, so the preview weighs every frame "
                                       "equally: %1")
                                        .arg(m_combinedFrameCountsFailure);
                }
                else if (!unread.isEmpty())
                {
                    uniformReason = tr("Reading how many frames each light combines from its header...");
                }
                else if (everyLightOnce)
                {
                    uniformReason = tr("No light states more than one combined frame in STACKCNT or NCOMBINE, so the "
                                       "stack weighs every frame equally.");
                }
            }
            else
            {
                const auto noiseByFrame = source == an::StackWeighting::Noise
                                              ? loadNoiseStatisticsFromDb(m_model, rowFrames)
                                              : std::unordered_map<QString, FrameNoiseStatistics>{};
                int carried = 0;
                for (int i = 0; i < rows.size(); ++i)
                {
                    frameIds.emplace_back(rows[i].id);
                    const auto noise = noiseByFrame.find(rows[i].id);
                    an::StackFrameMeasurement& measurement = measurements[static_cast<std::size_t>(i)];
                    measurement = ImageIntegration::stackFrameMeasurementFor(
                        rows[i].metrics, noise != noiseByFrame.end() ? noise->second : FrameNoiseStatistics{});
                    carried += ImageIntegration::carriesWeightingFigure(source, measurement) ? 1 : 0;
                }
                if (source == an::StackWeighting::Photometric && carried == 0)
                {
                    uniformReason = tr("Photometric weighting preview is using uniform weights because the current "
                                       "registration results do not contain usable photometric zero-point data.");
                    LOG_WARNING("FrameQualityDashboard: Photometric weighting preview falling back to uniform weights "
                                "because no zero-point data is available");
                }
                else if (carried < rows.size())
                {
                    // The stack weighs a set by a figure only when every frame carries it.
                    uniformReason = tr("%1 weighting preview is using uniform weights, as the stack does: only %2 of "
                                       "%3 frames carry %4.")
                                        .arg(mode)
                                        .arg(carried)
                                        .arg(rows.size())
                                        .arg(weightingFigureName(source));
                    if (source == an::StackWeighting::Noise)
                    {
                        uniformReason += QStringLiteral(" ") + tr("Run Registration to measure it.");
                    }
                }
            }

            if (!uniformReason.isEmpty())
            {
                bannerText = uniformReason;
            }
            else
            {
                const std::optional<FrameId> reference =
                    m_weightingConfig.referenceFrame.trimmed().isEmpty()
                        ? std::nullopt
                        : std::optional<FrameId>(FrameId(m_weightingConfig.referenceFrame));
                const StackWeightPreview preview = previewStackWeights(
                    ImageIntegration::stackWeightingRequestFor(settings, source, choice->starCountScaling),
                    measurements, ImageIntegration::stackWeightingReferenceIndex(frameIds, reference));
                for (int i = 0; i < raw.size() && i < static_cast<int>(preview.weights.size()); ++i)
                {
                    raw[i] = preview.weights[static_cast<std::size_t>(i)];
                }
                weightsPreviewBusy = preview.busy;
                bannerText = preview.note;
                if (source == an::StackWeighting::Photometric)
                {
                    QList<FrameQualityMetrics> metricsList;
                    metricsList.reserve(rows.size());
                    for (const auto& row : rows)
                    {
                        metricsList.push_back(row.metrics);
                    }
                    const QString coverage = ImageIntegration::summarizePhotometricCoverage(metricsList).warningText();
                    if (!coverage.isEmpty())
                    {
                        bannerText = bannerText.isEmpty() ? coverage : coverage + QStringLiteral("\n") + bannerText;
                    }
                }
            }
        }
    }
    if (m_weightingBanner && stackWeights)
    {
        m_weightingBanner->setText(bannerText);
        m_weightingBanner->setVisible(!bannerText.trimmed().isEmpty());
    }

    QVector<double> weightsForHistogram;
    weightsForHistogram.reserve(rows.size());

    double weightedExposure = 0.0;
    double heaviest = 0.0;
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        double w = stackWeights ? (std::max)(0.0, raw[i]) : clamp01(raw[i]);

        if (m_manualWeights.contains(row.id))
        {
            w = stackWeights ? (std::max)(0.0, m_manualWeights.value(row.id)) : clamp01(m_manualWeights.value(row.id));
        }

        m_computedWeights.insert(row.id, w);
        weightedExposure += row.exposure * w;
        heaviest = (std::max)(heaviest, w);
        weightsForHistogram.push_back(w);
    }
    // Each frame's exposure counts in proportion to its weight, the heaviest frame's whole: weights of mean one sum
    // to the raw exposure of equal frames whatever they are, and say nothing of what the weighting costs.
    m_lastEffectiveExposureSeconds = heaviest > 0.0 ? weightedExposure / heaviest : 0.0;

    // Update weight column
    QSignalBlocker blocker(m_table);
    const bool wasSorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);

    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        auto* nameItem = m_table->item(r, kFrameColumn);
        if (!nameItem)
        {
            continue;
        }
        const QString id = nameItem->data(Qt::UserRole).toString();

        auto* weightItem = m_table->item(r, kWeightColumn);
        if (!weightItem)
        {
            weightItem = new NumericSortItem(QStringLiteral("-"), -1.0);
            m_table->setItem(r, kWeightColumn, weightItem);
        }

        if (!m_computedWeights.contains(id))
        {
            weightItem->setText(QStringLiteral("-"));
            weightItem->setData(Qt::UserRole, -1.0);
            weightItem->setFlags(weightItem->flags() & ~Qt::ItemIsEditable);
            continue;
        }

        const double w = m_computedWeights.value(id);
        weightItem->setText(QString::number(w, 'f', 3));
        weightItem->setData(Qt::UserRole, w);
        weightItem->setFlags(weightItem->flags() | Qt::ItemIsEditable);
        if (m_manualWeights.contains(id))
        {
            weightItem->setForeground(
                QBrush(an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::WarningText)));
            weightItem->setToolTip(tr("Manual override"));
        }
        else
        {
            weightItem->setForeground(palette().text().color());
            weightItem->setToolTip(QString());
        }
    }
    m_table->setSortingEnabled(wasSorting);

    updateEffectiveExposureLabel();
    hist->setMeanNormalizedSemantics(stackWeights);
    hist->setWeights(weightsForHistogram);
    hist->setMinWeightFloor(stackWeights ? 0.0 : minFloor);

    if (weightsPreviewBusy && !m_weightsPreviewRetryPending)
    {
        // The session had no thread free for the stack's weighting: ask again shortly rather than wait on this thread.
        m_weightsPreviewRetryPending = true;
        QTimer::singleShot(kWeightPreviewRetryMilliseconds, this, [this]() {
            m_weightsPreviewRetryPending = false;
            if (m_weightingEnabled)
            {
                calculateWeights();
            }
        });
    }
}

void FrameQualityDashboard::readCombinedFrameCountsOf(const QStringList& paths)
{
    const an::consumer::EngineSession* session = ImageIntegration::detail::applicationEngineSession();
    if (m_combinedFrameCountsReading || paths.isEmpty() || session == nullptr)
    {
        if (session == nullptr)
        {
            m_combinedFrameCountsFailure =
                tr("The weights preview needs the application's engine session to read the lights' headers.");
        }
        return;
    }
    m_combinedFrameCountsReading = true;
    const quint64 generation = m_combinedFrameCountsGeneration;
    auto* watcher = new QFutureWatcher<CombinedFrameCounts>(this);
    QObject::connect(watcher, &QFutureWatcher<CombinedFrameCounts>::finished, this, [this, watcher, generation]() {
        const CombinedFrameCounts read = watcher->result();
        watcher->deleteLater();
        m_combinedFrameCountsReading = false;
        // Counts read before the dashboard was refreshed are let go of; the next preview reads them again.
        if (generation == m_combinedFrameCountsGeneration)
        {
            for (auto count = read.counts.constBegin(); count != read.counts.constEnd(); ++count)
            {
                m_combinedFrameCounts.insert(count.key(), count.value());
            }
            m_combinedFrameCountsFailure = read.failure;
        }
        if (m_weightingEnabled)
        {
            calculateWeights();
        }
    });
    watcher->setFuture(QtConcurrent::run([session, paths]() { return readCombinedFrameCounts(session, paths); }));
}

double FrameQualityDashboard::weightFor(int row) const
{
    if (!m_table || row < 0 || row >= m_table->rowCount())
    {
        return 0.0;
    }
    const auto* nameItem = m_table->item(row, kFrameColumn);
    if (!nameItem)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const QString id = nameItem->data(Qt::UserRole).toString();
    if (!m_weightingEnabled)
    {
        // When weighting is disabled, the preview is conceptually uniform.
        return 1.0;
    }
    return m_computedWeights.value(id, 0.0);
}

void FrameQualityDashboard::setManualWeight(int row, double weight)
{
    if (!m_table || row < 0 || row >= m_table->rowCount())
    {
        return;
    }
    auto* nameItem = m_table->item(row, kFrameColumn);
    if (!nameItem)
    {
        return;
    }
    const QString id = nameItem->data(Qt::UserRole).toString();
    const bool stackWeights = usesStackWeights(m_weightingConfig.weightingMode);
    m_manualWeights.insert(id, stackWeights ? (std::max)(0.0, weight) : clamp01(weight));
    if (m_weightingEnabled)
    {
        calculateWeights();
    }
}

bool FrameQualityDashboard::isManuallyOverridden(int row) const
{
    if (!m_table || row < 0 || row >= m_table->rowCount())
    {
        return false;
    }
    const auto* nameItem = m_table->item(row, kFrameColumn);
    if (!nameItem)
    {
        return false;
    }
    const QString id = nameItem->data(Qt::UserRole).toString();
    return m_manualWeights.contains(id);
}

double FrameQualityDashboard::effectiveExposureSeconds() const
{
    return m_lastEffectiveExposureSeconds;
}

QWidget* FrameQualityDashboard::weightHistogram() const
{
    return m_weightHistogram;
}

const QMap<QString, FilterStatistics>& FrameQualityDashboard::filterStatistics() const
{
    return m_filterStats;
}

void FrameQualityDashboard::computeFilterStatistics(const std::vector<FrameInfo>& frames)
{
    m_filterStats.clear();
    m_frameFilterMap.clear();

    if (!m_model || frames.empty())
    {
        return;
    }

    // Collect metrics grouped by filter.
    // If filter metadata is missing for ALL frames (typical for OSC/color cameras),
    // treat everything as one group.
    // If filter metadata is present for SOME frames but missing for others,
    // keep missing values as "(unknown)" and avoid normalizing them (to prevent
    // misleading cross-filter comparisons).
    std::unordered_map<QString, std::vector<FrameQualityMetrics>> metricsByFilter;

    bool hasAnyNamedFilter = false;
    for (const auto& frame : frames)
    {
        if (isFrameExcludedFromCurrentStack(frame))
        {
            continue;
        }
        const QString filter = frame.metadata.filter().trimmed();
        if (!filter.isEmpty())
        {
            hasAnyNamedFilter = true;
            break;
        }
    }

    for (const auto& frame : frames)
    {
        if (isFrameExcludedFromCurrentStack(frame))
        {
            continue;
        }

        auto metrics = m_model->frameQualityMetrics(frame.id);
        if (!metrics || !metrics->succeeded)
        {
            continue;
        }

        const QString filter = frame.metadata.filter().trimmed();
        const QString filterKey =
            filter.isEmpty() ? (hasAnyNamedFilter ? QStringLiteral("(unknown)") : QStringLiteral("(no filter)"))
                             : filter;

        m_frameFilterMap.insert(frame.id.canonicalPath(), filterKey);
        metricsByFilter[filterKey].push_back(*metrics);
    }

    // Compute statistics for each filter
    for (const auto& [filterKey, metricsList] : metricsByFilter)
    {
        // If some frames have filters but this frame doesn't, don't normalize:
        // the "unknown" group could contain mixed filters.
        if (hasAnyNamedFilter && filterKey == QStringLiteral("(unknown)"))
        {
            continue;
        }
        if (metricsList.size() < 2)
        {
            // Not enough frames to compute meaningful statistics
            continue;
        }

        FilterStatistics stats = FilterStatisticsEngine::computeStatistics(filterKey, metricsList);
        m_filterStats.insert(filterKey, stats);
    }
}

QString FrameQualityDashboard::buildMetricTooltip(const QString& metricName, double absoluteValue,
                                                  const QString& filterKey, bool higherIsBetter) const
{
    const QString unit = (metricName == QStringLiteral("FWHM")) ? QStringLiteral(" px") : QString();
    const int precision =
        (metricName == QStringLiteral("Eccentricity")) ? 3 : (metricName == QStringLiteral("Stars") ? 0 : 2);
    QString tooltip = tr("%1 (absolute): %2%3").arg(metricName).arg(absoluteValue, 0, 'f', precision).arg(unit);

    if (!m_filterStats.contains(filterKey))
    {
        if (filterKey == QStringLiteral("(unknown)"))
        {
            tooltip +=
                QStringLiteral("\n") + tr("(filter metadata missing; filter-relative normalization unavailable)");
        }
        else if (filterKey == QStringLiteral("(no filter)"))
        {
            tooltip += QStringLiteral("\n") +
                       tr("(no filter metadata; need at least 2 succeeded frames to compute normalization stats)");
        }
        else
        {
            tooltip += QStringLiteral("\n") + tr("(no filter statistics available)");
        }
        return tooltip;
    }

    const FilterStatistics& stats = m_filterStats[filterKey];
    double median = 0.0;
    double mad = 0.0;

    if (metricName == QStringLiteral("FWHM"))
    {
        median = stats.fwhmMedian;
        mad = stats.fwhmMad;
    }
    else if (metricName == QStringLiteral("SNR"))
    {
        median = stats.snrMedian;
        mad = stats.snrMad;
    }
    else if (metricName == QStringLiteral("Eccentricity"))
    {
        median = stats.eccMedian;
        mad = stats.eccMad;
    }
    else if (metricName == QStringLiteral("Stars"))
    {
        median = stats.starsMedian;
        mad = stats.starsMad;
    }
    else
    {
        return tooltip;
    }

    // Compute z-score
    double zScore = 0.0;
    if (mad > 1e-9)
    {
        zScore = (absoluteValue - median) / mad;
        // Flip sign for metrics where lower is better
        if (!higherIsBetter)
        {
            zScore = -zScore;
        }
    }

    // Convert z-score to percentile approximation using normal CDF
    // This gives a rough "better than X%" interpretation
    const double percentile = 0.5 * (1.0 + std::erf(zScore / std::sqrt(2.0)));

    QString comparison;
    if (zScore > 0.3)
    {
        comparison = tr("better than average");
    }
    else if (zScore < -0.3)
    {
        comparison = tr("worse than average");
    }
    else
    {
        comparison = tr("near average");
    }

    tooltip += QStringLiteral("\n\n");
    tooltip += tr("Filter group: %1 (%2 frames)").arg(filterKey).arg(stats.frameCount);
    tooltip += QStringLiteral("\n");
    tooltip += tr("Filter median: %1").arg(median, 0, 'f', 2);
    tooltip += QStringLiteral("\n");
    tooltip += tr("Filter MAD: %1").arg(mad, 0, 'f', 3);
    tooltip += QStringLiteral("\n\n");
    tooltip += tr("Z-score: %1%2 (%3)").arg(zScore >= 0 ? "+" : "").arg(zScore, 0, 'f', 2).arg(comparison);
    tooltip += QStringLiteral("\n");
    tooltip += tr("Percentile: %1%").arg(percentile * 100.0, 0, 'f', 0);

    return tooltip;
}

void FrameQualityDashboard::onFramesAdded()
{
    QSignalBlocker blocker(m_table);
    m_table->setRowCount(0);
    if (m_banner)
    {
        m_banner->setVisible(false);
    }
    if (m_weightingBanner)
    {
        m_weightingBanner->setVisible(false);
    }

    const auto key_opt = m_model->selectedRegistrationGroupKey();
    if (m_filterByRegistrationGroup && !key_opt.has_value())
    {
        setEmptyMessage(m_table, tr("Select a registration group in the Registration tab to view frame quality."));
        if (m_weightHistogram)
        {
            m_weightHistogram->setVisible(true);
            auto* hist = static_cast<WeightHistogramWidget*>(m_weightHistogram);
            hist->setSelectedFrameWeight(std::numeric_limits<double>::quiet_NaN());
            hist->setMeanNormalizedSemantics(false);
            hist->setWeights({});
        }
        updateEffectiveExposureLabel();
        return;
    }

    const GroupKey selected_key = key_opt.has_value() ? *key_opt : GroupKey{};
    const auto frames = m_model->lightFrames().frames();
    std::vector<FrameInfo> filtered;
    filtered.reserve(frames.size());
    for (const auto& frame : frames)
    {
        if (m_frameIdFilterActive && !m_frameIdFilter.contains(frame.id.canonicalPath()))
        {
            continue;
        }
        if (!m_filterByRegistrationGroup)
        {
            filtered.push_back(frame);
            continue;
        }

        const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(frame));
        if (machine_key.toString() == selected_key.toString())
        {
            filtered.push_back(frame);
        }
    }

    if (filtered.empty())
    {
        if (!m_filterByRegistrationGroup && !m_frameIdFilterActive)
        {
            if (m_weightHistogram)
            {
                m_weightHistogram->setVisible(true);
                auto* hist = static_cast<WeightHistogramWidget*>(m_weightHistogram);
                hist->setSelectedFrameWeight(std::numeric_limits<double>::quiet_NaN());
                hist->setMeanNormalizedSemantics(false);
                hist->setWeights({});
            }
            updateEffectiveExposureLabel();
            return;
        }

        if (m_frameIdFilterActive && m_frameIdFilter.isEmpty())
        {
            setEmptyMessage(m_table, tr("Select an integration group to view frame quality."));
        }
        else
        {
            setEmptyMessage(m_table, tr("No frames in the selected group."));
        }
        if (m_weightHistogram)
        {
            m_weightHistogram->setVisible(true);
            auto* hist = static_cast<WeightHistogramWidget*>(m_weightHistogram);
            hist->setSelectedFrameWeight(std::numeric_limits<double>::quiet_NaN());
            hist->setMeanNormalizedSemantics(false);
            hist->setWeights({});
        }
        updateEffectiveExposureLabel();
        return;
    }

    if (m_weightingBanner)
    {
        m_weightingBanner->setVisible(!m_weightingEnabled);
    }

    bool need_db = false;
    for (const auto& frame : filtered)
    {
        if (isFrameExcludedFromCurrentStack(frame))
        {
            continue;
        }
        if (!m_model->frameQualityMetrics(frame.id).has_value())
        {
            need_db = true;
            break;
        }
    }
    const auto db_metrics = need_db ? loadMetricsFromDb(m_model, filtered) : std::unordered_map<QString, DbMetricRow>{};

    int metrics_found = 0;
    int succeeded_found = 0;
    for (const auto& frame : filtered)
    {
        auto metrics = m_model->frameQualityMetrics(frame.id);
        if (!metrics.has_value())
        {
            const auto it = db_metrics.find(frame.id.canonicalPath());
            if (it != db_metrics.end())
            {
                m_model->setFrameQualityMetrics(frame.id, it->second.metrics);
                metrics = it->second.metrics;
            }
        }
        if (!metrics.has_value())
        {
            continue;
        }
        ++metrics_found;
        if (metrics->succeeded)
        {
            ++succeeded_found;
        }
    }

    if (m_banner)
    {
        const int frame_count = static_cast<int>(filtered.size());
        if (metrics_found <= 0)
        {
            if (m_model->outputDirectory().trimmed().isEmpty())
            {
                m_banner->setText(
                    tr("Frame quality metrics are not available.\n"
                       "Set an output directory and run Registration to compute Stars/FWHM/SNR/Eccentricity."));
            }
            else
            {
                m_banner->setText(tr("Frame quality metrics are not available yet for this registration group.\n"
                                     "Run Registration to compute Stars/FWHM/SNR/Eccentricity."));
            }
            m_banner->setVisible(true);
        }
        else if (metrics_found < frame_count)
        {
            m_banner->setText(tr("Frame quality metrics are incomplete (%1/%2 frames).\n"
                                 "Run Registration to compute the missing metrics before relying on outlier detection "
                                 "or weights preview.")
                                  .arg(metrics_found)
                                  .arg(frame_count));
            m_banner->setVisible(true);
        }
        else if (succeeded_found < metrics_found)
        {
            m_banner->setText(tr("Some frames failed registration (%1/%2 succeeded).\n"
                                 "Outlier detection and weights preview may be unreliable until registration succeeds.")
                                  .arg(succeeded_found)
                                  .arg(metrics_found));
            m_banner->setVisible(true);
        }
    }

    // Compute filter statistics for normalized metrics display
    computeFilterStatistics(filtered);

    m_table->setSortingEnabled(false);
    m_table->setRowCount(static_cast<int>(filtered.size()));

    int row = 0;
    for (const auto& frame : filtered)
    {
        const QString exclusionText = exclusionStateText(frame);
        auto* nameItem = new QTableWidgetItem(QFileInfo(frame.displayPath).fileName());
        nameItem->setData(Qt::UserRole, frame.id.canonicalPath());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setToolTip(exclusionText.isEmpty()
                                 ? frame.displayPath
                                 : QStringLiteral("%1\n\n%2").arg(frame.displayPath, exclusionText));
        m_table->setItem(row, 0, nameItem);

        auto metrics = m_model->frameQualityMetrics(frame.id);
        if (metrics)
        {
            // Get filter key for this frame for filter-relative tooltips
            const QString filterKey = m_frameFilterMap.value(frame.id.canonicalPath(), QStringLiteral("(unknown)"));

            // Compute score with filter context if available
            double score = frameQualityScore(*metrics);
            const FilterStatistics* filterStats = nullptr;
            if (m_filterStats.contains(filterKey))
            {
                filterStats = &m_filterStats[filterKey];
                score = frameQualityScore(*metrics, *filterStats);
            }

            const auto formatZ = [](double z) {
                return QStringLiteral("%1%2").arg(z >= 0.0 ? "+" : "").arg(z, 0, 'f', 2);
            };

            const bool hasValidFilterStats = (filterStats != nullptr && filterStats->isValid());
            QString scoringDetails;
            if (!metrics->succeeded)
            {
                scoringDetails = tr("Scoring: unavailable (registration failed)");
            }
            else if (hasValidFilterStats)
            {
                const auto normalized = FilterStatisticsEngine::normalizeMetrics(*metrics, *filterStats);
                scoringDetails = tr("Scoring: filter-normalized (z-scores)\n"
                                    "Z(FWHM): %1\n"
                                    "Z(SNR): %2\n"
                                    "Z(Ecc): %3\n"
                                    "Z(Stars): %4\n"
                                    "Z(ZP): %5\n"
                                    "Z(ZP Scatter): %6\n"
                                    "Z(Astro RMS): %7\n"
                                    "Z(Lim Mag): %8\n"
                                    "Z(Catalog): %9\n"
                                    "Combined Z: %10\n"
                                    "Score = clamp(50 + CombinedZ * 10, 0..100)")
                                     .arg(formatZ(normalized.fwhmZ))
                                     .arg(formatZ(normalized.snrZ))
                                     .arg(formatZ(normalized.eccZ))
                                     .arg(formatZ(normalized.starsZ))
                                     .arg(formatZ(normalized.zeroPointZ))
                                     .arg(formatZ(normalized.zeroPointScatterZ))
                                     .arg(formatZ(normalized.astrometricRmsZ))
                                     .arg(formatZ(normalized.limitingMagnitudeZ))
                                     .arg(formatZ(normalized.catalogMatchedZ))
                                     .arg(formatZ(normalized.combinedZ()));
            }
            else
            {
                const double clampedAbs = FilterStatisticsEngine::computeQualityScore(*metrics, std::nullopt);
                scoringDetails = tr("Scoring: absolute (fallback)\n"
                                    "Raw score: %1\n"
                                    "Clamped score: %2\n"
                                    "Score = log1p(stars)*10 + log1p(snr)*2 - fwhm*5 - ecc*10\n"
                                    "      + zero_point*4 - zero_point_scatter*20 - astro_rms*8\n"
                                    "      + log1p(catalog_matched)*2 + limiting_mag*0.5")
                                     .arg(frameQualityScore(*metrics), 0, 'f', 2)
                                     .arg(clampedAbs, 0, 'f', 2);
            }

            QString rowTooltip = tr("Frame: %1\n"
                                    "Score: %2\n"
                                    "Stars: %3\n"
                                    "FWHM: %4\n"
                                    "SNR: %5\n"
                                    "Eccentricity: %6\n"
                                    "Registration: %7\n"
                                    "Filter: %8\n\n%9")
                                     .arg(frame.displayPath)
                                     .arg(score, 0, 'f', 2)
                                     .arg(metrics->stars)
                                     .arg(metrics->fwhm, 0, 'f', 2)
                                     .arg(metrics->snr, 0, 'f', 1)
                                     .arg(metrics->eccentricity, 0, 'f', 3)
                                     .arg(metrics->succeeded ? tr("Succeeded") : tr("Failed"))
                                     .arg(filterKey)
                                     .arg(scoringDetails);
            if (!exclusionText.isEmpty())
            {
                rowTooltip.append(QStringLiteral("\n\n%1").arg(exclusionText));
            }

            auto* scoreItem = new NumericSortItem(QString::number(score, 'f', 2), score);
            scoreItem->setFlags(scoreItem->flags() & ~Qt::ItemIsEditable);
            scoreItem->setToolTip(rowTooltip);
            m_table->setItem(row, kScoreColumn, scoreItem);

            // Stars tooltip with filter statistics
            const QString starsTooltip =
                buildMetricTooltip(QStringLiteral("Stars"), static_cast<double>(metrics->stars), filterKey, true);
            auto* starsItem = new NumericSortItem(QString::number(metrics->stars), static_cast<double>(metrics->stars));
            starsItem->setFlags(starsItem->flags() & ~Qt::ItemIsEditable);
            starsItem->setToolTip(starsTooltip);
            m_table->setItem(row, kStarsColumn, starsItem);

            // FWHM tooltip with filter statistics (lower is better)
            const QString fwhmTooltip = buildMetricTooltip(QStringLiteral("FWHM"), metrics->fwhm, filterKey, false);
            auto* fwhmItem = new NumericSortItem(QString::number(metrics->fwhm, 'f', 2), metrics->fwhm);
            fwhmItem->setFlags(fwhmItem->flags() & ~Qt::ItemIsEditable);
            fwhmItem->setToolTip(fwhmTooltip);
            m_table->setItem(row, kFwhmColumn, fwhmItem);

            // SNR tooltip with filter statistics (higher is better)
            const QString snrTooltip = buildMetricTooltip(QStringLiteral("SNR"), metrics->snr, filterKey, true);
            auto* snrItem = new NumericSortItem(QString::number(metrics->snr, 'f', 1), metrics->snr);
            snrItem->setFlags(snrItem->flags() & ~Qt::ItemIsEditable);
            snrItem->setToolTip(snrTooltip);
            m_table->setItem(row, kSnrColumn, snrItem);

            // Eccentricity tooltip with filter statistics (lower is better)
            const QString eccTooltip =
                buildMetricTooltip(QStringLiteral("Eccentricity"), metrics->eccentricity, filterKey, false);
            auto* eccItem = new NumericSortItem(QString::number(metrics->eccentricity, 'f', 3), metrics->eccentricity);
            eccItem->setFlags(eccItem->flags() & ~Qt::ItemIsEditable);
            eccItem->setToolTip(eccTooltip);
            m_table->setItem(row, kEccColumn, eccItem);

            auto* weightItem = new NumericSortItem(QStringLiteral("-"), -1.0);
            weightItem->setFlags(weightItem->flags() & ~Qt::ItemIsEditable);
            weightItem->setToolTip(rowTooltip);
            m_table->setItem(row, kWeightColumn, weightItem);

            const bool excludedFromStack = isFrameExcludedFromCurrentStack(frame);
            if (!metrics->succeeded || excludedFromStack)
            {
                for (int c = 0; c < m_table->columnCount(); ++c)
                {
                    if (auto* item = m_table->item(row, c))
                        item->setForeground(excludedFromStack ? an::ui::tokens().colors.textTertiary
                                                              : an::ui::tokens().colors.statusError);
                }
            }
        }
        else
        {
            for (int c = 1; c < m_table->columnCount(); ++c)
            {
                auto* item = new NumericSortItem(QStringLiteral("-"), -1.0);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setToolTip(tr("Not available (run Registration for this group to compute metrics)."));
                m_table->setItem(row, c, item);
            }
            if (!exclusionText.isEmpty())
            {
                for (int c = 0; c < m_table->columnCount(); ++c)
                {
                    if (auto* item = m_table->item(row, c))
                    {
                        item->setForeground(an::ui::tokens().colors.textTertiary);
                        if (!item->toolTip().contains(exclusionText))
                        {
                            item->setToolTip(QStringLiteral("%1\n\n%2").arg(item->toolTip(), exclusionText).trimmed());
                        }
                    }
                }
            }
        }

        row++;
    }

    m_table->setSortingEnabled(true);

    if (m_weightHistogram)
    {
        m_weightHistogram->setVisible(true);
    }

    if (m_weightingEnabled)
    {
        calculateWeights();
    }
    else
    {
        // Weighting disabled: show a consistent preview by placing all weights at 1.0.
        QVector<double> weights;
        weights.reserve(m_table->rowCount());

        const auto key_opt = m_model ? m_model->selectedRegistrationGroupKey() : std::optional<GroupKey>{};
        const bool filterByGroup = m_filterByRegistrationGroup;
        const GroupKey selected_key = (filterByGroup && key_opt.has_value()) ? *key_opt : GroupKey{};

        const auto frames = m_model ? m_model->lightFrames().frames() : std::vector<FrameInfo>{};
        for (const auto& frame : frames)
        {
            if (filterByGroup)
            {
                if (!key_opt.has_value())
                {
                    break;
                }
                const GroupKey machine_key =
                    RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(frame));
                if (machine_key.toString() != selected_key.toString())
                {
                    continue;
                }
            }
            if (m_frameIdFilterActive && !m_frameIdFilter.contains(frame.id.canonicalPath()))
            {
                continue;
            }
            if (isFrameExcludedFromCurrentStack(frame))
            {
                continue;
            }
            auto metrics = m_model ? m_model->frameQualityMetrics(frame.id) : std::nullopt;
            if (!metrics || !metrics->succeeded)
            {
                continue;
            }
            weights.push_back(1.0);
        }

        m_computedWeights.clear();
        m_lastEffectiveExposureSeconds = 0.0;
        auto* hist = static_cast<WeightHistogramWidget*>(m_weightHistogram);
        hist->setSelectedFrameWeight(std::numeric_limits<double>::quiet_NaN());
        hist->setMeanNormalizedSemantics(false);
        hist->setMinWeightFloor(0.0);
        hist->setWeights(weights);
        updateEffectiveExposureLabel();
    }
}

void FrameQualityDashboard::onWeightingToggled(bool enabled)
{
    m_weightingEnabled = enabled;
    if (m_weightHistogram)
    {
        m_weightHistogram->setVisible(true);
    }

    if (m_weightingBanner)
    {
        if (!enabled)
        {
            // A weighting the group stores and its stack lets go of is named with the reason, not as one set to None.
            m_weightingBanner->setText(
                m_weightingDisabledReason.isEmpty()
                    ? tr("Weighting is disabled for this integration group (Stacking Settings → Weighting: None).")
                    : tr("Weighting is not applied to this integration group: %1").arg(m_weightingDisabledReason));
        }
        m_weightingBanner->setVisible(!enabled);
    }

    if (enabled)
    {
        calculateWeights();
    }
    else
    {
        m_computedWeights.clear();
        m_lastEffectiveExposureSeconds = 0.0;
        QSignalBlocker blocker(m_table);
        for (int r = 0; r < m_table->rowCount(); ++r)
        {
            if (auto* item = m_table->item(r, kWeightColumn))
            {
                item->setText(QStringLiteral("-"));
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            }
        }
        updateEffectiveExposureLabel();
        // Weighting disabled: keep the histogram visible and show a uniform distribution at 1.0.
        QVector<double> weights;

        const auto key_opt = m_model ? m_model->selectedRegistrationGroupKey() : std::optional<GroupKey>{};
        const bool filterByGroup = m_filterByRegistrationGroup;
        const GroupKey selected_key = (filterByGroup && key_opt.has_value()) ? *key_opt : GroupKey{};

        const auto frames = m_model ? m_model->lightFrames().frames() : std::vector<FrameInfo>{};
        for (const auto& frame : frames)
        {
            if (filterByGroup)
            {
                if (!key_opt.has_value())
                {
                    break;
                }
                const GroupKey machine_key =
                    RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(frame));
                if (machine_key.toString() != selected_key.toString())
                {
                    continue;
                }
            }
            if (m_frameIdFilterActive && !m_frameIdFilter.contains(frame.id.canonicalPath()))
            {
                continue;
            }
            if (isFrameExcludedFromCurrentStack(frame))
            {
                continue;
            }
            auto metrics = m_model ? m_model->frameQualityMetrics(frame.id) : std::nullopt;
            if (!metrics || !metrics->succeeded)
            {
                continue;
            }
            weights.push_back(1.0);
        }

        auto* hist = static_cast<WeightHistogramWidget*>(m_weightHistogram);
        hist->setSelectedFrameWeight(std::numeric_limits<double>::quiet_NaN());
        hist->setMeanNormalizedSemantics(false);
        hist->setMinWeightFloor(0.0);
        hist->setWeights(weights);
    }
}

void FrameQualityDashboard::onWeightItemChanged(QTableWidgetItem* item)
{
    if (!m_weightingEnabled || !item || item->column() != kWeightColumn)
    {
        return;
    }

    auto* nameItem = m_table->item(item->row(), kFrameColumn);
    if (!nameItem)
    {
        return;
    }

    bool ok = false;
    const double w = item->text().toDouble(&ok);
    if (!ok)
    {
        return;
    }

    const QString id = nameItem->data(Qt::UserRole).toString();
    const bool stackWeights = usesStackWeights(m_weightingConfig.weightingMode);
    m_manualWeights.insert(id, stackWeights ? (std::max)(0.0, w) : clamp01(w));
    calculateWeights();
}

void FrameQualityDashboard::updateEffectiveExposureLabel()
{
    if (!m_effectiveExposureLabel || !m_model)
    {
        return;
    }

    const auto key_opt = m_model->selectedRegistrationGroupKey();
    if (m_filterByRegistrationGroup && !key_opt.has_value())
    {
        if (m_weightingEnabled)
        {
            m_effectiveExposureLabel->setText(tr("Effective exposure: -"));
        }
        else
        {
            m_effectiveExposureLabel->setText(tr("Raw exposure: -"));
        }
        return;
    }

    const GroupKey selected_key = key_opt.has_value() ? *key_opt : GroupKey{};
    double rawExposure = 0.0;
    const auto frames = m_model->lightFrames().frames();
    for (const auto& frame : frames)
    {
        if (m_filterByRegistrationGroup)
        {
            const GroupKey machine_key =
                RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(frame));
            if (machine_key.toString() != selected_key.toString())
            {
                continue;
            }
        }
        if (m_frameIdFilterActive && !m_frameIdFilter.contains(frame.id.canonicalPath()))
        {
            continue;
        }
        if (frame.excluded)
        {
            continue;
        }
        auto metrics = m_model->frameQualityMetrics(frame.id);
        if (!metrics || !metrics->succeeded)
        {
            continue;
        }
        rawExposure += frame.metadata.exposureSeconds();
    }

    if (m_weightingEnabled)
    {
        m_effectiveExposureLabel->setText(tr("Effective exposure: %1 s (raw %2 s)")
                                              .arg(m_lastEffectiveExposureSeconds, 0, 'f', 1)
                                              .arg(rawExposure, 0, 'f', 1));
    }
    else
    {
        m_effectiveExposureLabel->setText(tr("Raw exposure: %1 s").arg(rawExposure, 0, 'f', 1));
    }
}

void FrameQualityDashboard::onDetectOutliersClicked()
{
    const auto key_opt = m_model->selectedRegistrationGroupKey();
    if (m_filterByRegistrationGroup && !key_opt.has_value())
    {
        return;
    }

    const GroupKey selected_key = key_opt.has_value() ? *key_opt : GroupKey{};
    const auto frames = m_model->lightFrames().frames();

    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    for (const auto& frame : frames)
    {
        if (m_filterByRegistrationGroup)
        {
            const GroupKey machine_key =
                RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(frame));
            if (machine_key.toString() != selected_key.toString())
            {
                continue;
            }
        }
        if (m_frameIdFilterActive && !m_frameIdFilter.contains(frame.id.canonicalPath()))
        {
            continue;
        }
        if (isFrameExcludedFromCurrentStack(frame))
        {
            continue;
        }
        if (auto m = m_model->frameQualityMetrics(frame.id))
        {
            candidates.emplace_back(frame.id, *m);
        }
    }

    if (candidates.empty())
        return;

    const double sigma = m_sigmaSpinBox->value();
    auto outliers = detectOutliers(candidates, sigma);

    // Select outliers in table
    m_table->clearSelection();
    auto* selection_model = m_table->selectionModel();
    auto* table_model = m_table->model();
    int first_selected_row = -1;
    for (const auto& id : outliers)
    {
        for (int r = 0; r < m_table->rowCount(); ++r)
        {
            auto* item = m_table->item(r, 0);
            if (item && item->data(Qt::UserRole).toString() == id.canonicalPath())
            {
                if (selection_model && table_model)
                {
                    const QModelIndex left = table_model->index(r, 0);
                    const QModelIndex right = table_model->index(r, m_table->columnCount() - 1);
                    selection_model->select(QItemSelection(left, right),
                                            QItemSelectionModel::Select | QItemSelectionModel::Rows);
                }
                if (first_selected_row < 0)
                {
                    first_selected_row = r;
                }
                break;
            }
        }
    }
    if (first_selected_row >= 0)
    {
        m_table->setCurrentCell(first_selected_row, kFrameColumn, QItemSelectionModel::NoUpdate);
        if (auto* item = m_table->item(first_selected_row, kFrameColumn))
        {
            m_table->scrollToItem(item);
        }
    }

    if (!outliers.empty())
    {
        ImageIntegration::detail::openNonBlockingMessageBox(
            this, QMessageBox::Information, tr("Outliers Detected"),
            tr("%1 outlier(s) identified and selected in the table.").arg(outliers.size()));
    }
    else
    {
        ImageIntegration::detail::openNonBlockingMessageBox(this, QMessageBox::Information, tr("No Outliers"),
                                                            tr("No significant quality outliers detected."));
    }
}

void FrameQualityDashboard::onExcludeSelectedClicked()
{
    auto selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty())
        return;

    if (isStackExclusionMode())
    {
        if (m_stackExclusionGroupKey.toString().trimmed().isEmpty())
        {
            return;
        }

        std::vector<StackExclusionChange> changes;
        changes.reserve(selected.size());
        for (const auto& idx : selected)
        {
            auto* item = m_table->item(idx.row(), 0);
            if (!item)
            {
                continue;
            }
            QString path = item->data(Qt::UserRole).toString();
            const auto frameOpt = m_model->frames(FrameType::Light).findById(FrameId(path));
            if (!frameOpt.has_value() || frameOpt->excluded ||
                m_model->isFrameStackExcluded(m_stackExclusionGroupKey, frameOpt->id))
            {
                continue;
            }
            changes.push_back(StackExclusionChange{path, false, true});
        }

        if (changes.empty())
        {
            return;
        }

        if (m_undoStack)
        {
            m_undoStack->push(new SetStackExclusionsCommand(m_model, m_stackExclusionGroupKey, std::move(changes)));
        }
        else
        {
            QStringList paths;
            paths.reserve(static_cast<int>(changes.size()));
            for (const auto& change : changes)
            {
                paths.push_back(change.path);
            }
            m_model->setFramesStackExcluded(m_stackExclusionGroupKey, paths, true);
        }
    }
    else
    {
        std::vector<FrameAttributeChange> changes;
        changes.reserve(selected.size());
        for (const auto& idx : selected)
        {
            auto* item = m_table->item(idx.row(), 0);
            if (!item)
            {
                continue;
            }
            QString path = item->data(Qt::UserRole).toString();
            const auto frameOpt = m_model->frames(FrameType::Light).findById(FrameId(path));
            if (!frameOpt.has_value() || frameOpt->excluded)
            {
                continue;
            }
            changes.push_back(FrameAttributeChange{path, frameOpt->excluded, true});
        }

        if (changes.empty())
        {
            return;
        }

        if (m_undoStack)
        {
            m_undoStack->push(new SetFrameAttributesCommand(m_model, FrameAttribute::Excluded, std::move(changes)));
        }
        else
        {
            m_model->beginBatchUpdate();
            for (const auto& change : changes)
            {
                m_model->updateFrameAttribute(change.path, FrameAttribute::Excluded, true);
            }
            m_model->endBatchUpdate();
        }
    }

    onFramesAdded();
}

void FrameQualityDashboard::onIncludeSelectedClicked()
{
    if (!isStackExclusionMode() || m_stackExclusionGroupKey.toString().trimmed().isEmpty())
    {
        return;
    }

    auto selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty())
    {
        return;
    }

    std::vector<StackExclusionChange> changes;
    changes.reserve(selected.size());
    for (const auto& idx : selected)
    {
        auto* item = m_table->item(idx.row(), 0);
        if (!item)
        {
            continue;
        }
        QString path = item->data(Qt::UserRole).toString();
        const auto frameOpt = m_model->frames(FrameType::Light).findById(FrameId(path));
        if (!frameOpt.has_value() || !m_model->isFrameStackExcluded(m_stackExclusionGroupKey, frameOpt->id))
        {
            continue;
        }
        changes.push_back(StackExclusionChange{path, true, false});
    }

    if (changes.empty())
    {
        return;
    }

    if (m_undoStack)
    {
        m_undoStack->push(new SetStackExclusionsCommand(m_model, m_stackExclusionGroupKey, std::move(changes)));
    }
    else
    {
        QStringList paths;
        paths.reserve(static_cast<int>(changes.size()));
        for (const auto& change : changes)
        {
            paths.push_back(change.path);
        }
        m_model->setFramesStackExcluded(m_stackExclusionGroupKey, paths, false);
    }

    onFramesAdded();
}
