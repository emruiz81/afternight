// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationTab.h"
#include "RegistrationTabUiBuilder.h"
#include "TabStyling.h"

#include "controls/IAppContext.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialogAutoTrackAdapter.h"
#include "processes/ImageIntegration/workflow/RegistrationWorkAnalyzer.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "RegistrationMetricsDataAccess.h"

#include <AfterNightPlatform/Logging.h>

#include <QBoxLayout>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QHash>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFutureWatcher>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QShortcut>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QUndoStack>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <unordered_map>

#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/widgets/QualityDistributionWidget.h"
#include "theme/AnDesignTokens.h"
#include "processes/ImageIntegration/widgets/NumericSortItem.h"

#include "RegistrationTabImpl.h"

void RegistrationTab::Impl::updateChartData()
{
    if (!chartWidget)
        return;

    const RegistrationGroupEntry* group = selectedGroup();
    if (!group || group->frames.empty())
    {
        chartWidget->clearData();
        return;
    }

    const auto metricsMap = loadRegistrationMetrics(*group);
    // Even if empty, we might want to show points without metrics? No, chart needs metrics.
    // But we have points in the group.

    QVector<ImageIntegration::ChartDataPoint> points;
    points.reserve(static_cast<int>(group->frames.size()));

    const QString refPath = model->registrationReferenceFrame(group->key).value_or(FrameId()).canonicalPath();

    const QualityThresholdConfig thresholds = currentQualityThresholdConfig();
    const FrameScoreContext scoreContext = buildFrameScoreContext(*group);

    for (const auto& frameId : group->frames)
    {
        const QString path = frameId.canonicalPath();
        ImageIntegration::ChartDataPoint pt;
        pt.frameId = path;
        pt.displayName = QFileInfo(path).fileName();
        pt.groupKey = group->key.toString();
        pt.isReference = (path == refPath);

        if (auto info = model->lightFrames().findById(frameId); info.has_value())
        {
            pt.filterName = info->metadata.filter();
            pt.isExcluded = info->excluded;
            pt.captureTime = info->metadata.capturedAtUtc();
        }

        const auto it = metricsMap.find(path);
        if (it != metricsMap.end() && it->second.metrics.succeeded)
        {
            pt.hasMetrics = true;
            const auto& m = it->second.metrics;
            pt.stars = m.stars;
            pt.fwhm = m.fwhm;
            pt.snr = m.snr;
            pt.eccentricity = m.eccentricity;
            pt.score = qualityScoreForFrame(frameId, m, scoreContext);
            pt.failsThreshold = frameFailsQualityThresholds(m, pt.score, thresholds);
        }
        else
        {
            // If no metrics, maybe skip or show as 0?
            // Chart handles empty/no metric points by filtering or showing as 0.
            // We'll add it but hasMetrics is false.
            continue; // Skip frames without metrics for now, or the chart will show 0s.
        }

        points.append(pt);
    }

    QVector<ImageIntegration::ChartGroupInfo> groups;
    ImageIntegration::ChartGroupInfo gInfo;
    gInfo.key = group->key.toString();
    gInfo.displayName = group->key.toString();
    gInfo.color = an::ui::tokens().colors.accentPrimary;
    groups.append(gInfo);

    chartWidget->setData(points, groups);
    updateChartThresholds();
}

const WorkflowManifest* RegistrationTab::Impl::currentRegistrationAnalysisManifest()
{
    if (registrationAnalysisManifestPrepared)
    {
        return registrationAnalysisManifest.has_value() ? &*registrationAnalysisManifest : nullptr;
    }

    registrationAnalysisManifestPrepared = true;
    try
    {
        const WorkflowExecutionController manifestBuilder(model, q);
        registrationAnalysisManifest = manifestBuilder.buildRegistrationOnlyManifestAll(false);
    }
    catch (const std::exception& error)
    {
        registrationAnalysisManifestError = RegistrationTab::tr("Current workflow facts could not be prepared: %1")
                                                .arg(QString::fromUtf8(error.what()));
    }
    catch (...)
    {
        registrationAnalysisManifestError = RegistrationTab::tr("Current workflow facts could not be prepared.");
    }

    return registrationAnalysisManifest.has_value() ? &*registrationAnalysisManifest : nullptr;
}

std::vector<RegistrationGroupEntry> RegistrationTab::Impl::collectRegistrationGroups() const
{
    std::vector<RegistrationGroupEntry> groups;
    const auto& map = model->registrationGroups();

    groups.reserve(map.size());
    for (const auto& entry : map)
    {
        groups.push_back({entry.first, entry.second});
    }
    return groups;
}

void RegistrationTab::Impl::setEmptyMessage(QTableWidget* table, const QString& message)
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
    item->setForeground(QBrush(ImageIntegration::Style::mutedTextColor()));
    table->setItem(0, 0, item);
}

void RegistrationTab::Impl::updateGroupTable()
{
    QString previously_selected_machine_key;
    if (ui.tblRegistrationGroups && ui.tblRegistrationGroups->selectionModel())
    {
        const auto selected = ui.tblRegistrationGroups->selectionModel()->selectedRows();
        if (!selected.isEmpty())
        {
            if (auto* item = ui.tblRegistrationGroups->item(selected.front().row(), 0))
            {
                previously_selected_machine_key = item->data(Qt::UserRole).toString();
            }
        }
    }

    cachedGroups = collectRegistrationGroups();

    if (!ui.tblRegistrationGroups)
    {
        return;
    }

    ui.tblRegistrationGroups->setRowCount(0);
    ui.tblRegistrationGroups->clearContents();
    ui.tblRegistrationGroups->clearSpans();

    if (cachedGroups.empty())
    {
        setEmptyMessage(ui.tblRegistrationGroups, RegistrationTab::tr("No registration groups available."));
        return;
    }

    // Optional: load group-level summary from registration metrics DB.
    bool db_open = false;

    struct GroupSummary
    {
        int total = 0;
        int succeeded = 0;
        std::optional<FrameId> bestFrame;
        QHash<QString, bool> succeededByFrame;
    };

    std::unordered_map<QString, GroupSummary> groupSummaries;
    groupSummaries.reserve(cachedGroups.size());

    db_open = executeMetricsDb([&](QSqlDatabase& db) {
        for (const auto& group : cachedGroups)
        {
            const QString group_key = RegistrationGroupEngine::toMachineGroupKey(group.key).toString();

            int desired_version = 0;
            {
                const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(group.key);
                auto it = model->registrationSettings().find(machine_key);
                if (it != model->registrationSettings().end())
                {
                    desired_version = it->second.version;
                }
            }

            const auto has_rows_for = [&](const int version) -> bool {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT 1 FROM registration_metrics WHERE group_key = :group_key AND "
                                         "settings_version = :v LIMIT 1"));
                q.bindValue(QStringLiteral(":group_key"), group_key);
                q.bindValue(QStringLiteral(":v"), version);
                return q.exec() && q.next();
            };

            int effective_version = desired_version;
            // Check if desired_version has rows; if not, fall back to MAX version.
            if (!has_rows_for(effective_version))
            {
                QSqlQuery q(db);
                q.prepare(QStringLiteral(
                    "SELECT MAX(settings_version) FROM registration_metrics WHERE group_key = :group_key"));
                q.bindValue(QStringLiteral(":group_key"), group_key);
                if (q.exec() && q.next())
                {
                    effective_version = q.value(0).toInt();
                }
                else
                {
                    continue;
                }
            }

            if (!has_rows_for(effective_version))
            {
                continue;
            }

            GroupSummary summary;

            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "SELECT frame_id, stars, fwhm, snr, eccentricity, succeeded, "
                "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, astrometric_rms_arcsec, "
                "limiting_magnitude, catalog_matched_stars "
                "FROM registration_metrics "
                "WHERE group_key = :group_key AND settings_version = :v"));
            q.bindValue(QStringLiteral(":group_key"), group_key);
            q.bindValue(QStringLiteral(":v"), effective_version);
            if (!q.exec())
            {
                continue;
            }

            std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
            while (q.next())
            {
                const QString frame_id = q.value(0).toString();
                FrameQualityMetrics m;
                m.stars = q.value(1).toInt();
                m.fwhm = q.value(2).toDouble();
                m.snr = q.value(3).toDouble();
                m.eccentricity = q.value(4).toDouble();
                m.succeeded = (q.value(5).toInt() != 0);
                m.photometric_zero_point = q.value(6).toDouble();
                m.zero_point_scatter = q.value(7).toDouble();
                m.astrometric_rms_pixels = q.value(8).toDouble();
                m.astrometric_rms_arcsec = q.value(9).toDouble();
                m.limiting_magnitude = q.value(10).toDouble();
                m.catalog_matched_stars = q.value(11).toInt();

                ++summary.total;
                if (m.succeeded)
                {
                    ++summary.succeeded;
                }

                summary.succeededByFrame.insert(FrameId(frame_id).canonicalPath(), m.succeeded);
                candidates.emplace_back(FrameId(frame_id), m);
            }

            summary.bestFrame = selectBestReferenceFrame(candidates);
            groupSummaries.emplace(group_key, std::move(summary));
        }
    });

    ui.tblRegistrationGroups->setRowCount(static_cast<int>(cachedGroups.size()));

    const auto light_frames = model->lightFrames().frames();
    std::unordered_map<FrameId, const FrameInfo*> frames_by_id;
    frames_by_id.reserve(light_frames.size());
    for (const auto& frame : light_frames)
    {
        frames_by_id.emplace(frame.id, &frame);
    }

    int row = 0;
    for (const auto& group : cachedGroups)
    {
        QString groupLabel = group.key.toString();
        if (groupLabel.trimmed().isEmpty())
        {
            groupLabel = RegistrationTab::tr("[Ungrouped]");
        }

        // Frames column should show included/total (non-excluded / all frames in the group).
        int total_frames_in_group = 0;
        int included_frames_in_group = 0;
        for (const auto& frame : light_frames)
        {
            if (RegistrationGroupKey::fromFrame(frame) != group.key)
            {
                continue;
            }
            ++total_frames_in_group;
            if (!frame.excluded)
            {
                ++included_frames_in_group;
            }
        }

        const QString frames_display =
            (total_frames_in_group > 0)
                ? QStringLiteral("%1/%2").arg(included_frames_in_group).arg(total_frames_in_group)
                : QString::number(group.frames.size());

        const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(group.key);

        auto* groupItem = new QTableWidgetItem(groupLabel);
        groupItem->setData(Qt::UserRole, machine_key.toString());
        ui.tblRegistrationGroups->setItem(row, 0, groupItem);

        // Algorithm column - show transformation type from settings
        QString algorithmText = registrationTransformDisplayName(RegistrationSettings{});
        auto settings_it = model->registrationSettings().find(machine_key);
        bool auto_reference_enabled = true;
        if (settings_it != model->registrationSettings().end())
        {
            algorithmText = registrationTransformDisplayName(settings_it->second);
            auto_reference_enabled = settings_it->second.autoReferenceDuringRegistration;
        }

        const auto ref = model->registrationReferenceFrame(group.key);
        QString referenceName = RegistrationTab::tr("Auto");
        if (!auto_reference_enabled && ref.has_value())
        {
            referenceName = QFileInfo(ref->canonicalPath()).fileName();
            if (referenceName.isEmpty())
            {
                referenceName = ref->toString();
            }
        }

        std::optional<GroupSummary> summary;
        if (db_open)
        {
            const auto it = groupSummaries.find(machine_key.toString());
            if (it != groupSummaries.end())
            {
                summary = it->second;
            }
        }

        if (!auto_reference_enabled && !ref.has_value() && summary.has_value() && summary->bestFrame.has_value())
        {
            QString best_name = QFileInfo(summary->bestFrame->canonicalPath()).fileName();
            if (best_name.isEmpty())
            {
                best_name = summary->bestFrame->toString();
            }
            referenceName = RegistrationTab::tr("Auto: %1").arg(best_name);
        }

        // Status is based on CURRENT included frames (i.e. frames in cachedGroups).
        // This avoids showing "Complete" when new included frames lack metrics.
        QString status_text = RegistrationTab::tr("Pending");
        const int included_count = static_cast<int>(group.frames.size());
        int metrics_present_count = 0;
        int succeeded_count = 0;
        int failed_count = 0;
        int missing_metrics_count = 0;

        for (const auto& id : group.frames)
        {
            bool has_metrics = false;
            bool succeeded = false;

            if (summary.has_value())
            {
                const auto it = summary->succeededByFrame.find(id.canonicalPath());
                if (it != summary->succeededByFrame.end())
                {
                    has_metrics = true;
                    succeeded = it.value();
                }
            }

            if (!has_metrics)
            {
                if (const auto m = model->frameQualityMetrics(id); m.has_value())
                {
                    has_metrics = true;
                    succeeded = m->succeeded;
                }
            }

            if (!has_metrics)
            {
                ++missing_metrics_count;
                continue;
            }

            ++metrics_present_count;
            if (succeeded)
            {
                ++succeeded_count;
            }
            else
            {
                ++failed_count;
            }
        }

        if (included_count <= 0)
        {
            status_text = RegistrationTab::tr("Pending");
        }
        else if (metrics_present_count <= 0)
        {
            status_text = RegistrationTab::tr("Pending");
        }
        else if (missing_metrics_count == 0 && failed_count == 0 && succeeded_count >= included_count)
        {
            status_text = RegistrationTab::tr("Complete");
        }
        else
        {
            status_text = RegistrationTab::tr("Partial");
        }

        QString status_icon;
        if (status_text == RegistrationTab::tr("Complete"))
        {
            status_icon = kOkIcon;
        }
        else if (status_text == RegistrationTab::tr("Partial"))
        {
            status_icon = kWarnIcon;
        }
        else
        {
            status_icon = kExcludedIcon;
        }

        QString tooltip_common = RegistrationTab::tr("Registration group: %1\n"
                                                     "Resolution: %2\n"
                                                     "Object: %3\n"
                                                     "Frames: %4\n"
                                                     "Group key: %5")
                                     .arg(groupLabel,
                                          group.key.resolution.trimmed().isEmpty() ? RegistrationTab::tr("-")
                                                                                   : group.key.resolution.trimmed(),
                                          group.key.objectName.trimmed().isEmpty() ? RegistrationTab::tr("-")
                                                                                   : group.key.objectName.trimmed(),
                                          frames_display, machine_key.toCompactString());

        groupItem->setToolTip(tooltip_common +
                              RegistrationTab::tr("\n\nDouble-click to open registration settings for this group."));

        auto* frames_item = new QTableWidgetItem(frames_display);
        frames_item->setToolTip(
            tooltip_common +
            RegistrationTab::tr("\n\nFrames shown as included/total (non-excluded / all frames in this group)."));
        ui.tblRegistrationGroups->setItem(row, 1, frames_item);

        // Filters / exposures come from actual frame metadata in this group.
        std::vector<QString> unique_filters;
        unique_filters.reserve(8);
        std::vector<double> unique_exposures;
        unique_exposures.reserve(8);

        for (const auto& id : group.frames)
        {
            const auto it = frames_by_id.find(id);
            if (it == frames_by_id.end() || !it->second)
            {
                continue;
            }

            const FrameInfo& frame = *it->second;
            const QString filter = frame.metadata.filter().trimmed();
            if (!filter.isEmpty())
            {
                if (std::find(unique_filters.begin(), unique_filters.end(), filter) == unique_filters.end())
                {
                    unique_filters.push_back(filter);
                }
            }

            const double exposure = frame.metadata.exposureSeconds();
            if (exposure > 0.0)
            {
                const auto approx_equal = [&](double existing) { return std::abs(existing - exposure) < 1e-3; };
                if (std::none_of(unique_exposures.begin(), unique_exposures.end(), approx_equal))
                {
                    unique_exposures.push_back(exposure);
                }
            }
        }

        std::sort(unique_filters.begin(), unique_filters.end());
        std::sort(unique_exposures.begin(), unique_exposures.end());

        const auto format_exposure = [](double seconds) {
            const double rounded = std::round(seconds);
            if (std::abs(seconds - rounded) < 1e-3)
            {
                return RegistrationTab::tr("%1s").arg(static_cast<int>(rounded));
            }
            return RegistrationTab::tr("%1s").arg(seconds, 0, 'f', 1);
        };

        QStringList unique_filters_list;
        unique_filters_list.reserve(static_cast<int>(unique_filters.size()));
        for (const auto& f : unique_filters)
        {
            unique_filters_list << f;
        }

        QString filters_text =
            unique_filters_list.isEmpty() ? RegistrationTab::tr("-") : unique_filters_list.join(QStringLiteral(", "));
        QStringList filters_tip_lines;
        filters_tip_lines << tooltip_common;
        filters_tip_lines << QString();
        if (unique_filters_list.isEmpty())
        {
            filters_tip_lines << RegistrationTab::tr("Filters: (not available)");
        }
        else
        {
            filters_tip_lines << RegistrationTab::tr("Filters (%1):").arg(unique_filters_list.size());
            for (const auto& f : unique_filters_list)
            {
                filters_tip_lines << RegistrationTab::tr("- %1").arg(f);
            }
        }

        auto* filters_item = new QTableWidgetItem(filters_text);
        filters_item->setToolTip(filters_tip_lines.join(QStringLiteral("\n")));
        ui.tblRegistrationGroups->setItem(row, 2, filters_item);

        QString exposures_text = RegistrationTab::tr("-");
        QStringList exposures_tip_lines;
        exposures_tip_lines << tooltip_common;
        exposures_tip_lines << QString();
        if (unique_exposures.empty())
        {
            exposures_tip_lines << RegistrationTab::tr("Exposures: (not available)");
        }
        else if (unique_exposures.size() == 1)
        {
            exposures_text = format_exposure(unique_exposures.front());
            exposures_tip_lines << RegistrationTab::tr("Exposure: %1").arg(exposures_text);
        }
        else if (unique_exposures.size() <= 3)
        {
            QStringList parts;
            for (double s : unique_exposures)
            {
                parts << format_exposure(s);
            }
            exposures_text = parts.join(QStringLiteral(", "));
            exposures_tip_lines
                << RegistrationTab::tr("Exposures (%1): %2").arg(unique_exposures.size()).arg(exposures_text);
        }
        else
        {
            exposures_text = RegistrationTab::tr("%1..%2").arg(format_exposure(unique_exposures.front()),
                                                               format_exposure(unique_exposures.back()));
            exposures_tip_lines << RegistrationTab::tr("Exposures (%1):").arg(unique_exposures.size());
            exposures_tip_lines << RegistrationTab::tr("- Range: %1").arg(exposures_text);
        }

        auto* exposures_item = new QTableWidgetItem(exposures_text);
        exposures_item->setToolTip(exposures_tip_lines.join(QStringLiteral("\n")));
        ui.tblRegistrationGroups->setItem(row, 3, exposures_item);

        QString alignment_text = RegistrationTab::tr("Standard");
        if (settings_it != model->registrationSettings().end())
        {
            const QString mode = settings_it->second.alignmentMode.trimmed();
            if (mode == QStringLiteral("mosaic"))
            {
                alignment_text = RegistrationTab::tr("Mosaic");
            }
            else if (mode == QStringLiteral("intersection"))
            {
                alignment_text = RegistrationTab::tr("Intersection");
            }
            else if (mode == QStringLiteral("standard"))
            {
                alignment_text = RegistrationTab::tr("Standard");
            }
        }

        auto* alignment_item = new QTableWidgetItem(alignment_text);
        alignment_item->setToolTip(tooltip_common + RegistrationTab::tr("\n\nAlignment mode: %1\n"
                                                                        "Change via Registration Settings.")
                                                        .arg(alignment_text));
        ui.tblRegistrationGroups->setItem(row, 4, alignment_item);

        auto* algorithm_item = new QTableWidgetItem(algorithmText);
        algorithm_item->setToolTip(tooltip_common + RegistrationTab::tr("\n\nTransformation used for registration: %1\n"
                                                                        "Change via Registration Settings.")
                                                        .arg(algorithmText));
        ui.tblRegistrationGroups->setItem(row, 5, algorithm_item);

        auto* reference_item = new QTableWidgetItem(referenceName);
        if (auto_reference_enabled)
        {
            reference_item->setToolTip(
                tooltip_common +
                RegistrationTab::tr(
                    "\n\nReference frame: Auto\n"
                    "Reference selection is automatic and will be computed when executing Registration."));
        }
        else
        {
            reference_item->setToolTip(tooltip_common +
                                       RegistrationTab::tr("\n\nReference frame: %1\n"
                                                           "This is the alignment target for the group.")
                                           .arg(referenceName));
        }
        ui.tblRegistrationGroups->setItem(row, 6, reference_item);

        auto* status_item = new QTableWidgetItem(status_icon + QStringLiteral(" ") + status_text);
        QStringList status_tip;
        status_tip << tooltip_common;
        status_tip << QString();
        status_tip << RegistrationTab::tr("Registration metrics status:");
        status_tip << RegistrationTab::tr("- Pending: no metrics available for this group");
        status_tip << RegistrationTab::tr("- Partial: metrics missing and/or some frames failed");
        status_tip << RegistrationTab::tr("- Complete: all included frames have metrics and succeeded");
        status_tip << QString();
        status_tip << RegistrationTab::tr("Included frames: %1").arg(included_count);
        status_tip << RegistrationTab::tr("Metrics available: %1 / %2").arg(metrics_present_count).arg(included_count);
        if (metrics_present_count > 0)
        {
            status_tip << RegistrationTab::tr("Succeeded: %1 / %2").arg(succeeded_count).arg(metrics_present_count);
            if (failed_count > 0)
            {
                status_tip << RegistrationTab::tr("Failed: %1").arg(failed_count);
            }
        }
        if (missing_metrics_count > 0)
        {
            status_tip << RegistrationTab::tr("Missing metrics: %1").arg(missing_metrics_count);
        }
        status_item->setToolTip(status_tip.join(QStringLiteral("\n")));
        ui.tblRegistrationGroups->setItem(row, 7, status_item);
        ++row;
    }

    // Keep only the content-based columns tight; Group/Reference stretch, Status stays fixed.
    for (int col = 0; col < ui.tblRegistrationGroups->columnCount(); ++col)
    {
        const auto mode = ui.tblRegistrationGroups->horizontalHeader()->sectionResizeMode(col);
        if (mode != QHeaderView::Fixed && mode != QHeaderView::Stretch)
        {
            ui.tblRegistrationGroups->resizeColumnToContents(col);
        }
    }

    if (ui.tblRegistrationGroups->rowCount() <= 0)
    {
        return;
    }

    // Preserve selection across refreshes (by machine key).
    if (!previously_selected_machine_key.trimmed().isEmpty())
    {
        for (int r = 0; r < ui.tblRegistrationGroups->rowCount(); ++r)
        {
            auto* item = ui.tblRegistrationGroups->item(r, 0);
            if (item && item->data(Qt::UserRole).toString() == previously_selected_machine_key)
            {
                ui.tblRegistrationGroups->selectRow(r);
                return;
            }
        }
    }

    ui.tblRegistrationGroups->selectRow(0);
}

const RegistrationGroupEntry* RegistrationTab::Impl::selectedGroup() const
{
    if (!ui.tblRegistrationGroups || cachedGroups.empty())
    {
        return nullptr;
    }

    const auto selected = ui.tblRegistrationGroups->selectionModel()->selectedRows();
    if (selected.isEmpty())
    {
        return nullptr;
    }

    const int row = selected.front().row();
    if (row < 0 || row >= static_cast<int>(cachedGroups.size()))
    {
        return nullptr;
    }

    return &cachedGroups[row];
}

QString RegistrationTab::Impl::qualityFilterKeyForFrame(const FrameId& frameId, const bool hasAnyNamedFilter) const
{
    QString filter;
    if (auto info = model->lightFrames().findById(frameId); info.has_value())
    {
        filter = info->metadata.filter().trimmed();
    }

    if (!filter.isEmpty())
    {
        return filter;
    }

    return hasAnyNamedFilter ? QStringLiteral("(unknown)") : QStringLiteral("(no filter)");
}

FrameScoreContext RegistrationTab::Impl::buildFrameScoreContext(const RegistrationGroupEntry& group) const
{
    FrameScoreContext context;

    for (const auto& frameId : group.frames)
    {
        if (auto info = model->lightFrames().findById(frameId); info.has_value() && !info->excluded)
        {
            if (!info->metadata.filter().trimmed().isEmpty())
            {
                context.hasAnyNamedFilter = true;
                break;
            }
        }
    }

    QHash<QString, std::vector<FrameQualityMetrics>> metricsByFilter;
    metricsByFilter.reserve(static_cast<int>(group.frames.size()));
    for (const auto& frameId : group.frames)
    {
        bool isExcluded = false;
        if (auto info = model->lightFrames().findById(frameId); info.has_value())
        {
            isExcluded = info->excluded;
        }
        if (isExcluded)
        {
            continue;
        }

        const auto metrics = model->frameQualityMetrics(frameId);
        if (!metrics.has_value() || !metrics->succeeded)
        {
            continue;
        }

        const QString filterKey = qualityFilterKeyForFrame(frameId, context.hasAnyNamedFilter);
        metricsByFilter[filterKey].push_back(*metrics);
    }

    context.filterStats.reserve(metricsByFilter.size());
    for (auto it = metricsByFilter.begin(); it != metricsByFilter.end(); ++it)
    {
        if (context.hasAnyNamedFilter && it.key() == QStringLiteral("(unknown)"))
        {
            continue;
        }
        if (it.value().size() < 2)
        {
            continue;
        }

        context.filterStats.insert(it.key(), FilterStatisticsEngine::computeStatistics(it.key(), it.value()));
    }

    return context;
}

double RegistrationTab::Impl::qualityScoreForFrame(const FrameId& frameId, const FrameQualityMetrics& metrics,
                                                   const FrameScoreContext& context) const
{
    const QString filterKey = qualityFilterKeyForFrame(frameId, context.hasAnyNamedFilter);
    const auto it = context.filterStats.constFind(filterKey);
    if (it != context.filterStats.constEnd())
    {
        return frameQualityScore(metrics, it.value());
    }

    return frameQualityScore(metrics);
}

void RegistrationTab::Impl::updateFrameTable()
{
    if (!ui.tblFrameQuality)
    {
        return;
    }

    displayedFrames.clear();

    if (ui.lblFrameQualityBanner)
    {
        ui.lblFrameQualityBanner->setVisible(false);
    }

    ui.tblFrameQuality->setRowCount(0);
    ui.tblFrameQuality->clearContents();
    ui.tblFrameQuality->clearSpans();

    // Disable sorting during population to prevent row reordering while setting item properties
    ui.tblFrameQuality->setSortingEnabled(false);

    const RegistrationGroupEntry* group = selectedGroup();
    if (!group || group->frames.empty())
    {
        setEmptyMessage(ui.tblFrameQuality, RegistrationTab::tr("Select a registration group to view frame quality."));
        if (chartWidget)
        {
            chartWidget->clearData();
        }
        return;
    }

    displayedFrames = group->frames;
    QSet<QString> seen_paths;
    for (const auto& id : displayedFrames)
    {
        seen_paths.insert(id.canonicalPath());
    }

    for (const auto& frame : model->lightFrames().frames())
    {
        if (!frame.excluded)
        {
            continue;
        }

        if (RegistrationGroupKey::fromFrame(frame) != group->key)
        {
            continue;
        }

        const QString path = frame.id.canonicalPath();
        if (!seen_paths.contains(path))
        {
            displayedFrames.push_back(frame.id);
            seen_paths.insert(path);
        }
    }

    const auto metrics_by_frame = loadRegistrationMetrics(*group);

    // Show error message if database access failed AND we have no fallback in model
    bool dbError = !lastDbError.isEmpty();

    const int frame_count = static_cast<int>(group->frames.size());
    int metrics_found = 0;
    int succeeded_found = 0;

    std::vector<FrameQualityMetrics> allMetrics;
    allMetrics.reserve(frame_count);

    // Filter metadata summary for safe normalization decisions
    bool hasAnyNamedFilter = false;
    for (const auto& frameId : group->frames)
    {
        if (auto info = model->lightFrames().findById(frameId); info.has_value() && !info->excluded)
        {
            const QString filter = info->metadata.filter().trimmed();
            if (!filter.isEmpty())
            {
                hasAnyNamedFilter = true;
                break;
            }
        }
    }

    QHash<QString, QString> frameToFilter;
    frameToFilter.reserve(static_cast<int>(displayedFrames.size()));

    QHash<QString, std::vector<FrameQualityMetrics>> metricsByFilter;

    // First pass: collect metrics and stats for ALL displayed frames (including excluded)
    for (const auto& frameId : displayedFrames)
    {
        QString filterKey = QStringLiteral("(no filter)");
        bool isExcluded = false;
        if (auto info = model->lightFrames().findById(frameId); info.has_value())
        {
            isExcluded = info->excluded;
            const QString filter = info->metadata.filter().trimmed();
            filterKey = filter.isEmpty()
                            ? (hasAnyNamedFilter ? QStringLiteral("(unknown)") : QStringLiteral("(no filter)"))
                            : filter;
        }
        frameToFilter.insert(frameId.canonicalPath(), filterKey);

        const QString key = frameId.canonicalPath();
        const auto it = metrics_by_frame.find(key);

        std::optional<FrameQualityMetrics> m_opt;

        if (it != metrics_by_frame.end())
        {
            m_opt = it->second.metrics;
            // Sync to model
            model->setFrameQualityMetrics(frameId, *m_opt);
        }
        else
        {
            // Fallback to model
            m_opt = model->frameQualityMetrics(frameId);
        }

        if (m_opt.has_value())
        {
            ++metrics_found;
            if (m_opt->succeeded)
            {
                ++succeeded_found;
                allMetrics.push_back(*m_opt);

                if (!isExcluded)
                {
                    metricsByFilter[filterKey].push_back(*m_opt);
                }
            }
        }
    }

    if (metrics_found == 0 && dbError)
    {
        setEmptyMessage(ui.tblFrameQuality, RegistrationTab::tr("Database error: %1").arg(lastDbError));
        if (chartWidget)
        {
            chartWidget->clearData();
        }
        return;
    }

    GroupStatistics stats = computeGroupStatistics(allMetrics);

    // Compute per-filter robust statistics for tooltips and filter-fair scoring
    QHash<QString, FilterStatistics> filterStats;
    filterStats.reserve(metricsByFilter.size());

    for (auto it = metricsByFilter.begin(); it != metricsByFilter.end(); ++it)
    {
        const QString filterKey = it.key();

        // If some frames have a filter but others don't, don't normalize "unknown".
        if (hasAnyNamedFilter && filterKey == QStringLiteral("(unknown)"))
        {
            continue;
        }
        if (it.value().size() < 2)
        {
            continue;
        }

        filterStats.insert(filterKey, FilterStatisticsEngine::computeStatistics(filterKey, it.value()));
    }

    if (ui.lblFrameQualityBanner)
    {
        ImageIntegration::Style::applyBannerStyle(ui.lblFrameQualityBanner,
                                                  ImageIntegration::Style::BannerTone::Warning, 4, 6);

        if (metrics_found <= 0)
        {
            if (model->outputDirectory().trimmed().isEmpty())
            {
                ui.lblFrameQualityBanner->setText(RegistrationTab::tr(
                    "Frame quality metrics are not available.\n"
                    "Set an output directory and run Registration to compute Stars/FWHM/SNR/Eccentricity."));
            }
            else
            {
                ui.lblFrameQualityBanner->setText(
                    RegistrationTab::tr("Frame quality metrics are not available yet for this registration group.\n"
                                        "Run Registration to compute Stars/FWHM/SNR/Eccentricity."));
            }
            ui.lblFrameQualityBanner->setVisible(true);
        }
        else if (metrics_found < frame_count)
        {
            ui.lblFrameQualityBanner->setText(
                RegistrationTab::tr("Frame quality metrics are incomplete (%1/%2 frames).\n"
                                    "Run Registration to compute the missing metrics before relying on outlier "
                                    "detection or auto-reference.")
                    .arg(metrics_found)
                    .arg(frame_count));
            ui.lblFrameQualityBanner->setVisible(true);
        }
        else if (succeeded_found < metrics_found)
        {
            ui.lblFrameQualityBanner->setText(
                RegistrationTab::tr(
                    "Some frames failed registration (%1/%2 succeeded).\n"
                    "Auto-reference and outlier detection may be unreliable until registration succeeds.")
                    .arg(succeeded_found)
                    .arg(metrics_found));
            ui.lblFrameQualityBanner->setVisible(true);
        }
    }

    ui.tblFrameQuality->setRowCount(static_cast<int>(displayedFrames.size()));

    // Theme detection
    const bool isDarkTheme = ui.tblFrameQuality->palette().color(QPalette::Text).lightness() > 128;

    // Identify reference frame
    QString referencePath;
    if (auto ref = model->registrationReferenceFrame(group->key); ref.has_value())
    {
        referencePath = ref->canonicalPath();
    }

    // Define row highlight colors based on theme (Mid-point saturation)
    const QColor refRowColor = ImageIntegration::Style::referenceRowColor();
    const QColor excRowColor = ImageIntegration::Style::excludedRowColor();
    const QColor failRowColor = ImageIntegration::Style::failedRowColor();

    int row = 0;
    for (const auto& frameId : displayedFrames)
    {
        QString name = QFileInfo(frameId.canonicalPath()).fileName();
        if (name.isEmpty())
        {
            name = frameId.toString();
        }

        // Determine status
        bool isReference = (!referencePath.isEmpty() && frameId.canonicalPath() == referencePath);
        bool isExcluded = false;
        if (auto info = model->lightFrames().findById(frameId); info.has_value())
        {
            isExcluded = info->excluded;
        }

        auto* statusItem = new QTableWidgetItem();
        statusItem->setTextAlignment(Qt::AlignCenter);
        ui.tblFrameQuality->setItem(row, kStatusCol, statusItem);

        const QString canonical = frameId.canonicalPath();
        const QString filterKey = frameToFilter.value(canonical, QStringLiteral("(no filter)"));

        auto* frameItem = new QTableWidgetItem(name);
        frameItem->setToolTip(RegistrationTab::tr("Path: %1\nFilter: %2").arg(canonical).arg(filterKey));
        frameItem->setData(Qt::UserRole, canonical);
        ui.tblFrameQuality->setItem(row, kFrameCol, frameItem);

        auto* filterItem = new QTableWidgetItem(filterKey);
        filterItem->setToolTip(RegistrationTab::tr("Filter: %1").arg(filterKey));
        ui.tblFrameQuality->setItem(row, kFilterCol, filterItem);

        const QString key = frameId.canonicalPath();
        const auto it = metrics_by_frame.find(key);
        std::optional<FrameQualityMetrics> m_opt;
        bool isFailed = false;

        if (it != metrics_by_frame.end())
        {
            m_opt = it->second.metrics;
        }
        else
        {
            m_opt = model->frameQualityMetrics(frameId);
        }

        if (!m_opt.has_value())
        {
            // Status icon + tooltip
            if (isReference)
            {
                statusItem->setText(kReferenceIcon);
                statusItem->setToolTip(
                    RegistrationTab::tr("Registration reference frame for this group\nMetrics not available yet"));
            }
            else if (isExcluded)
            {
                statusItem->setText(kExcludedIcon);
                statusItem->setToolTip(
                    RegistrationTab::tr("Excluded from registration and stacking\nMetrics not available"));
            }
            else
            {
                statusItem->setText(kWarnIcon);
                statusItem->setToolTip(RegistrationTab::tr("Registration metrics not available (run Registration)"));
            }

            ui.tblFrameQuality->setItem(row, kStarsCol, new QTableWidgetItem(RegistrationTab::tr("-")));
            ui.tblFrameQuality->setItem(row, kFwhmCol, new QTableWidgetItem(RegistrationTab::tr("-")));
            ui.tblFrameQuality->setItem(row, kSnrCol, new QTableWidgetItem(RegistrationTab::tr("-")));
            ui.tblFrameQuality->setItem(row, kEccCol, new QTableWidgetItem(RegistrationTab::tr("-")));
            // Sort missing scores below 0 so they don't interleave with real numeric scores.
            ui.tblFrameQuality->setItem(row, kScoreCol, new NumericSortItem(RegistrationTab::tr("-"), -1.0));
            ui.tblFrameQuality->setItem(row, kQualityCol, new NumericSortItem(RegistrationTab::tr("-"), -2));
        }
        else
        {
            const FrameQualityMetrics& m = *m_opt;
            isFailed = !m.succeeded;

            // Status icon + tooltip
            if (isExcluded)
            {
                statusItem->setText(kExcludedIcon);
                statusItem->setToolTip(RegistrationTab::tr("Excluded from registration and stacking"));
            }
            else if (!m.succeeded)
            {
                statusItem->setText(kFailIcon);
                statusItem->setToolTip(RegistrationTab::tr("Registration failed for this frame"));
            }
            else if (isReference)
            {
                statusItem->setText(kReferenceIcon);
                statusItem->setToolTip(RegistrationTab::tr("Registration reference frame for this group"));
            }
            else
            {
                statusItem->setText(kOkIcon);
                statusItem->setToolTip(RegistrationTab::tr("Included in registration/stacking"));
            }

            auto* stars_item = new NumericSortItem(QString::number(m.stars), m.stars);
            stars_item->setData(Qt::EditRole, m.stars);
            if (m.succeeded)
            {
                const QColor metricBackground =
                    getMetricColor(static_cast<double>(m.stars), stats.starsQ1, stats.starsQ3, true, isDarkTheme);
                stars_item->setBackground(metricBackground);
                stars_item->setForeground(QBrush(resolvedTableCellTextColor(ui.tblFrameQuality, metricBackground)));
                stars_item->setToolTip(buildFilterNormalizedMetricTooltip(
                    QStringLiteral("Stars"), static_cast<double>(m.stars), filterKey, filterStats, true));
            }
            ui.tblFrameQuality->setItem(row, kStarsCol, stars_item);

            auto* fwhm_item = new QTableWidgetItem(QString::number(m.fwhm, 'f', 3));
            fwhm_item->setData(Qt::EditRole, m.fwhm);
            if (m.succeeded)
            {
                const QColor metricBackground = getMetricColor(m.fwhm, stats.fwhmQ1, stats.fwhmQ3, false, isDarkTheme);
                fwhm_item->setBackground(metricBackground);
                fwhm_item->setForeground(QBrush(resolvedTableCellTextColor(ui.tblFrameQuality, metricBackground)));
                fwhm_item->setToolTip(
                    buildFilterNormalizedMetricTooltip(QStringLiteral("FWHM"), m.fwhm, filterKey, filterStats, false));
            }
            ui.tblFrameQuality->setItem(row, kFwhmCol, fwhm_item);

            auto* snr_item = new QTableWidgetItem(QString::number(m.snr, 'f', 3));
            snr_item->setData(Qt::EditRole, m.snr);
            if (m.succeeded)
            {
                const QColor metricBackground = getMetricColor(m.snr, stats.snrQ1, stats.snrQ3, true, isDarkTheme);
                snr_item->setBackground(metricBackground);
                snr_item->setForeground(QBrush(resolvedTableCellTextColor(ui.tblFrameQuality, metricBackground)));
                snr_item->setToolTip(
                    buildFilterNormalizedMetricTooltip(QStringLiteral("SNR"), m.snr, filterKey, filterStats, true));
            }
            ui.tblFrameQuality->setItem(row, kSnrCol, snr_item);

            auto* ecc_item = new QTableWidgetItem(QString::number(m.eccentricity, 'f', 3));
            ecc_item->setData(Qt::EditRole, m.eccentricity);
            if (m.succeeded)
            {
                const QColor metricBackground =
                    getMetricColor(m.eccentricity, stats.eccQ1, stats.eccQ3, false, isDarkTheme);
                ecc_item->setBackground(metricBackground);
                ecc_item->setForeground(QBrush(resolvedTableCellTextColor(ui.tblFrameQuality, metricBackground)));
                ecc_item->setToolTip(buildFilterNormalizedMetricTooltip(QStringLiteral("Ecc"), m.eccentricity,
                                                                        filterKey, filterStats, false));
            }
            ui.tblFrameQuality->setItem(row, kEccCol, ecc_item);

            double score = frameQualityScore(m);
            const FilterStatistics* statsPtr = nullptr;
            if (filterStats.contains(filterKey))
            {
                const auto itStats = filterStats.constFind(filterKey);
                if (itStats != filterStats.constEnd())
                {
                    statsPtr = &itStats.value();
                    score = frameQualityScore(m, *statsPtr);
                }
            }

            auto* score_item = new NumericSortItem(QString::number(score, 'f', 3), score);
            score_item->setData(Qt::EditRole, score);
            if (!m.succeeded)
            {
                score_item->setForeground(QBrush(an::ui::tokens().colors.statusError));
            }

            const auto formatZ = [](double z) {
                return QStringLiteral("%1%2").arg(z >= 0.0 ? "+" : "").arg(z, 0, 'f', 2);
            };

            const bool hasValidFilterStats = (statsPtr != nullptr && statsPtr->isValid());
            QString scoringDetails;
            if (!m.succeeded)
            {
                scoringDetails = RegistrationTab::tr("Scoring: unavailable (registration failed)");
            }
            else if (hasValidFilterStats)
            {
                const auto normalized = FilterStatisticsEngine::normalizeMetrics(m, *statsPtr);
                scoringDetails = RegistrationTab::tr("Scoring: filter-normalized (z-scores)\n"
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
                const double clampedAbs = FilterStatisticsEngine::computeQualityScore(m, std::nullopt);
                scoringDetails = RegistrationTab::tr("Scoring: absolute (fallback)\n"
                                                     "Raw score: %1\n"
                                                     "Clamped score: %2\n"
                                                     "Score = log1p(stars)*10 + log1p(snr)*2 - fwhm*5 - ecc*10\n"
                                                     "      + zero_point*4 - zero_point_scatter*20 - astro_rms*8\n"
                                                     "      + log1p(catalog_matched)*2 + limiting_mag*0.5")
                                     .arg(frameQualityScore(m), 0, 'f', 2)
                                     .arg(clampedAbs, 0, 'f', 2);
            }

            score_item->setToolTip(
                RegistrationTab::tr("Score: %1\nStars: %2\nFWHM: %3\nSNR: %4\nEcc: %5\nFilter: %6\n\n%7")
                    .arg(score, 0, 'f', 2)
                    .arg(m.stars)
                    .arg(m.fwhm, 0, 'f', 3)
                    .arg(m.snr, 0, 'f', 3)
                    .arg(m.eccentricity, 0, 'f', 3)
                    .arg(filterKey)
                    .arg(scoringDetails));
            ui.tblFrameQuality->setItem(row, kScoreCol, score_item);

            if (m.succeeded)
            {
                QualityLevel level = computeFrameQuality(m, stats);
                auto* qualityItem = new NumericSortItem(qualityIcon(level), static_cast<int>(level));
                qualityItem->setToolTip(qualityTooltip(level));
                qualityItem->setTextAlignment(Qt::AlignCenter);
                ui.tblFrameQuality->setItem(row, kQualityCol, qualityItem);
            }
            else
            {
                auto* failItem = new NumericSortItem(QStringLiteral("❌"), -1);
                failItem->setToolTip(RegistrationTab::tr("Registration Failed"));
                failItem->setTextAlignment(Qt::AlignCenter);
                ui.tblFrameQuality->setItem(row, kQualityCol, failItem);
            }
        }

        // Apply row highlighting
        if (isExcluded)
        {
            for (int col = 0; col < ui.tblFrameQuality->columnCount(); ++col)
            {
                if (auto* item = ui.tblFrameQuality->item(row, col))
                {
                    item->setBackground(excRowColor);
                    item->setForeground(QBrush(resolvedTableCellTextColor(ui.tblFrameQuality, excRowColor,
                                                                          ImageIntegration::Style::mutedTextColor())));
                }
            }
        }
        else if (isFailed)
        {
            for (int col = 0; col < ui.tblFrameQuality->columnCount(); ++col)
            {
                if (auto* item = ui.tblFrameQuality->item(row, col))
                {
                    item->setBackground(failRowColor);
                    item->setForeground(QBrush(resolvedTableCellTextColor(ui.tblFrameQuality, failRowColor,
                                                                          ImageIntegration::Style::mutedTextColor())));
                }
            }
        }
        else if (isReference)
        {
            for (int col = 0; col < ui.tblFrameQuality->columnCount(); ++col)
            {
                if (auto* item = ui.tblFrameQuality->item(row, col))
                {
                    item->setBackground(refRowColor);
                    item->setForeground(QBrush(resolvedTableCellTextColor(ui.tblFrameQuality, refRowColor,
                                                                          an::ui::tokens().colors.textOnAccent)));
                }
            }
        }

        ++row;
    }

    // Re-enable sorting now that all items are populated with their properties
    ui.tblFrameQuality->setSortingEnabled(true);

    updateChartData();

    // Resize content-based columns only (not fixed Status/Quality columns)
    for (int col = 0; col < ui.tblFrameQuality->columnCount(); ++col)
    {
        if (ui.tblFrameQuality->horizontalHeader()->sectionResizeMode(col) != QHeaderView::Fixed &&
            ui.tblFrameQuality->horizontalHeader()->sectionResizeMode(col) != QHeaderView::Stretch)
        {
            ui.tblFrameQuality->resizeColumnToContents(col);
        }
    }
}

std::unordered_map<QString, MetricRow>
RegistrationTab::Impl::loadRegistrationMetrics(const RegistrationGroupEntry& group)
{
    std::unordered_map<QString, MetricRow> metrics;
    lastDbError.clear();

    const QString outputDir = model ? model->outputDirectory().trimmed() : QString();
    if (outputDir.isEmpty())
    {
        return metrics;
    }

    const QString group_key = RegistrationGroupEngine::toMachineGroupKey(group.key).toString();

    // Prefer the model's current settings version; fall back to latest available in DB.
    const int desired_version = getDesiredVersion(group.key);
    const int effective_version = metricsDataAccess.resolveEffectiveVersion(outputDir, group_key, desired_version);

    if (effective_version < 0)
    {
        return metrics;
    }

    const bool executed = metricsDataAccess.execute(outputDir, [&](QSqlDatabase& db) {
        // Single batch query for all frames in the group.
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT frame_id, stars, fwhm, snr, eccentricity, succeeded, "
            "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, astrometric_rms_arcsec, "
            "limiting_magnitude, catalog_matched_stars, output_path "
            "FROM registration_metrics WHERE group_key = :group_key AND settings_version = :v"));
        q.bindValue(QStringLiteral(":group_key"), group_key);
        q.bindValue(QStringLiteral(":v"), effective_version);

        if (!q.exec())
        {
            lastDbError = RegistrationTab::tr("Failed to query metrics: %1").arg(q.lastError().text());
            return;
        }

        while (q.next())
        {
            const QString frame_id = q.value(0).toString();
            MetricRow row;
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
            row.outputPath = q.value(12).toString();
            metrics.emplace(frame_id, row);
        }
    });

    if (!executed && lastDbError.isEmpty())
    {
        const QString accessError = metricsDataAccess.lastError().trimmed();
        if (!accessError.isEmpty())
        {
            lastDbError = RegistrationTab::tr("Failed to query metrics: %1").arg(accessError);
        }
    }

    return metrics;
}

int RegistrationTab::Impl::getDesiredVersion(const RegistrationGroupKey& key) const
{
    const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(key);
    auto it = model->registrationSettings().find(machine_key);
    if (it != model->registrationSettings().end())
    {
        return it->second.version;
    }
    return 0;
}

void RegistrationTab::Impl::selectFrameInQualityTable(const QString& canonicalPath)
{
    if (!ui.tblFrameQuality || canonicalPath.trimmed().isEmpty())
    {
        return;
    }

    const auto flashRow = [this](int row, const QString& canonical) {
        if (!ui.tblFrameQuality || row < 0 || row >= ui.tblFrameQuality->rowCount())
        {
            return;
        }

        auto* keyItem = ui.tblFrameQuality->item(row, kFrameCol);
        if (!keyItem || keyItem->data(Qt::UserRole).toString().trimmed() != canonical)
        {
            return;
        }

        const QColor flashColor = ImageIntegration::Style::flashRowColor();
        const QColor flashTextColor = resolvedTableCellTextColor(ui.tblFrameQuality, flashColor);

        struct CellStyle
        {
            QBrush background;
            QBrush foreground;
            QFont font;
            bool valid = false;
        };

        std::vector<CellStyle> before;
        before.resize(static_cast<size_t>(ui.tblFrameQuality->columnCount()));

        for (int col = 0; col < ui.tblFrameQuality->columnCount(); ++col)
        {
            if (auto* item = ui.tblFrameQuality->item(row, col))
            {
                before[static_cast<size_t>(col)] =
                    CellStyle{item->background(), item->foreground(), item->font(), true};
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
                item->setBackground(QBrush(flashColor));
                item->setForeground(QBrush(flashTextColor));
            }
        }

        QTimer::singleShot(900, q, [this, row, canonical, before]() {
            if (!ui.tblFrameQuality || row < 0 || row >= ui.tblFrameQuality->rowCount())
            {
                return;
            }

            auto* key = ui.tblFrameQuality->item(row, kFrameCol);
            if (!key || key->data(Qt::UserRole).toString().trimmed() != canonical)
            {
                return;
            }

            for (int col = 0; col < ui.tblFrameQuality->columnCount(); ++col)
            {
                if (auto* item = ui.tblFrameQuality->item(row, col))
                {
                    const auto& prev = before[static_cast<size_t>(col)];
                    if (!prev.valid)
                    {
                        continue;
                    }
                    item->setBackground(prev.background);
                    item->setForeground(prev.foreground);
                    item->setFont(prev.font);
                }
            }
        });
    };

    for (int row = 0; row < ui.tblFrameQuality->rowCount(); ++row)
    {
        auto* item = ui.tblFrameQuality->item(row, kFrameCol);
        if (!item)
        {
            continue;
        }

        if (item->data(Qt::UserRole).toString().trimmed() == canonicalPath)
        {
            ui.tblFrameQuality->setFocus(Qt::OtherFocusReason);
            ui.tblFrameQuality->selectRow(row);
            ui.tblFrameQuality->scrollToItem(item);
            flashRow(row, canonicalPath);
            break;
        }
    }
}
