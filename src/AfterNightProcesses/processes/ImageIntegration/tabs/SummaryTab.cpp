// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SummaryTab.h"
#include "SummaryTabUiBuilder.h"

#include "theme/AnDesignTokens.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/tabs/WorkflowHistoryDelegate.h"
#include "processes/ImageIntegration/tabs/WorkflowHistoryModel.h"
#include "processes/ImageIntegration/tabs/RunHistoryThumbnailCache.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/core/FileImport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPushButton>
#include <QStringList>
#include <QSet>
#include <QSizePolicy>
#include <QStyle>
#include <QStorageInfo>
#include <QTextBrowser>
#include <QToolButton>
#include <QTimeZone>
#include <QTimer>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QListView>
#include <QTreeView>
#include <QAbstractItemView>
#include <QDesktopServices>
#include <QUrl>

#include "app/DesktopUtils.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <verdigris/wobjectimpl.h>
#include <QScrollArea>

W_REGISTER_ARGTYPE(FixTarget)
W_OBJECT_IMPL(SummaryTab)

namespace
{

class ClickableIssueCard final : public QFrame
{
public:
    explicit ClickableIssueCard(std::function<void()> activated, QWidget* parent = nullptr)
        : QFrame(parent), m_activated(std::move(activated))
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(SummaryTab::tr("Open issue target"));
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton && rect().contains(event->pos()) && m_activated)
        {
            event->accept();
            m_activated();
            return;
        }

        QFrame::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event &&
            (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) &&
            m_activated)
        {
            event->accept();
            m_activated();
            return;
        }

        QFrame::keyPressEvent(event);
    }

private:
    std::function<void()> m_activated;
};

QIcon summaryIcon(const QString& iconName)
{
    if (iconName.isEmpty())
    {
        return QIcon();
    }

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        return iconProvider->getIcon(iconName);
    }

    return QIcon();
}

QPixmap summaryPixmap(const QString& iconName, const QSize& size)
{
    return summaryIcon(iconName).pixmap(size);
}

struct RunHistoryEntry
{
    QDateTime whenUtc;
    QString result;
    QString executionId;
    QString groupKey;
    QString output;
    QString tooltip;
    QString runDir;
    QString outputPath;
    QString summary;
    QString previewPath;
    QString reportPath;
    qint64 outputBytes = 0;
    bool hasManifest = false;
};

struct ComparableMasterSelection
{
    QStringList paths;
    QStringList labels;
    QStringList contexts;
    QVariantList historyRows;
};

std::optional<QJsonObject> readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
    {
        return std::nullopt;
    }

    return doc.object();
}

QDateTime parseRunTimestamp(const QString& run_dir_name)
{
    const QString trimmed = run_dir_name.size() > 19 ? run_dir_name.left(19) : run_dir_name;
    const QDateTime parsed = QDateTime::fromString(trimmed, QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    return parsed.isValid() ? parsed : QDateTime();
}

QString boolLabel(bool value, const QString& on_label, const QString& off_label)
{
    return value ? on_label : off_label;
}

QString normalizedHistoryToken(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("-"))
    {
        return {};
    }

    return trimmed.simplified().toCaseFolded();
}

QString normalizedHistoryFilename(const QString& filename)
{
    const QString trimmed = filename.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    return QFileInfo(trimmed).completeBaseName().trimmed().toCaseFolded();
}

QString historyRoleToken(const QModelIndex& index, int role)
{
    return normalizedHistoryToken(index.data(role).toString());
}

bool sameNonEmptyHistoryRole(const QModelIndex& lhs, const QModelIndex& rhs, int role)
{
    const QString left = historyRoleToken(lhs, role);
    const QString right = historyRoleToken(rhs, role);
    return !left.isEmpty() && left == right;
}

bool conflictingHistoryRole(const QModelIndex& lhs, const QModelIndex& rhs, int role)
{
    const QString left = historyRoleToken(lhs, role);
    const QString right = historyRoleToken(rhs, role);
    return !left.isEmpty() && !right.isEmpty() && left != right;
}

bool historyGroupsCanCompare(const QModelIndex& source, const QModelIndex& candidate)
{
    if (!source.isValid() || !candidate.isValid() || !candidate.data(WorkflowHistoryModel::IsGroupItemRole).toBool() ||
        candidate.data(WorkflowHistoryModel::StatusRole).toString() != QStringLiteral("success"))
    {
        return false;
    }

    if (sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::RawGroupKeyRole) ||
        sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::GroupKeyRole))
    {
        return true;
    }

    const QString sourceFilename =
        normalizedHistoryFilename(source.data(WorkflowHistoryModel::OutputFilenameRole).toString());
    const QString candidateFilename =
        normalizedHistoryFilename(candidate.data(WorkflowHistoryModel::OutputFilenameRole).toString());
    if (!sourceFilename.isEmpty() && sourceFilename == candidateFilename)
    {
        return true;
    }

    const std::array<int, 3> rolesToCheck = {WorkflowHistoryModel::FilterRole, WorkflowHistoryModel::ObjectRole,
                                             WorkflowHistoryModel::ExposureRole};
    for (const int role : rolesToCheck)
    {
        if (conflictingHistoryRole(source, candidate, role))
        {
            return false;
        }
    }

    // Drizzle/interpolation choices intentionally do not participate in matching:
    // same target/filter/exposure masters are useful to compare even when output sizes differ.
    return sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::FilterRole) ||
           sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::ObjectRole) ||
           sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::ExposureRole) ||
           sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::RegistrationGroupKeyRole);
}

int historyContextMatchScore(const QModelIndex& source, const QModelIndex& candidate)
{
    int score = 0;
    const QString sourceFilename =
        normalizedHistoryFilename(source.data(WorkflowHistoryModel::OutputFilenameRole).toString());
    const QString candidateFilename =
        normalizedHistoryFilename(candidate.data(WorkflowHistoryModel::OutputFilenameRole).toString());
    if (!sourceFilename.isEmpty() && sourceFilename == candidateFilename)
    {
        score += 16;
    }
    if (sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::RawGroupKeyRole))
    {
        score += 8;
    }
    if (sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::GroupKeyRole))
    {
        score += 4;
    }
    if (sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::FilterRole))
    {
        score += 2;
    }
    if (sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::ObjectRole))
    {
        score += 2;
    }
    if (sameNonEmptyHistoryRole(source, candidate, WorkflowHistoryModel::ExposureRole))
    {
        score += 1;
    }
    return score;
}

QString formatDuration(double seconds);

struct MasterFileMetadata
{
    QSize dimensions;
    double integrationSeconds = 0.0;
};

[[nodiscard]] QString resolutionTextForDimensions(const QSize& dimensions)
{
    if (!dimensions.isValid() || dimensions.width() <= 0 || dimensions.height() <= 0)
    {
        return {};
    }
    return QStringLiteral("%1x%2").arg(dimensions.width()).arg(dimensions.height());
}

[[nodiscard]] MasterFileMetadata masterFileMetadataForPath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() || !QFileInfo::exists(trimmed))
    {
        return {};
    }

    if (!ImageIntegration::detail::isReadableImagePath(trimmed))
    {
        return {};
    }

    // The summary is built on the GUI thread, which never waits for the session: while the session has no thread free
    // the master's header is not read and the caller falls back to what the run recorded.
    const an::consumer::EngineSession* session = ImageIntegration::detail::applicationEngineSession();
    if (session == nullptr)
    {
        return {};
    }
    an::Result<an::consumer::OperationScope> scope = session->tryAdmit(ImageIntegration::importProbeRequest());
    if (!scope.hasValue())
    {
        return {};
    }

    const std::string utf8Path = trimmed.toUtf8().toStdString();
    const an::Result<an::CodecImageInfo> probed = an::probeImage(
        scope->engine(), scope->execution(), an::CodecReadRequest{.path = utf8Path, .validatePayload = false});
    if (!probed.hasValue())
    {
        return {};
    }

    MasterFileMetadata result;
    const std::uint32_t width = probed->descriptor.extent().width();
    const std::uint32_t height = probed->descriptor.extent().height();
    constexpr auto kLargestDimension = static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    if (width > 0u && height > 0u && width <= kLargestDimension && height <= kLargestDimension)
    {
        result.dimensions = QSize(static_cast<int>(width), static_cast<int>(height));
    }

    // The exposure a master states: its acquisition record, else its exposure cards, as an imported frame reads it.
    const FrameMetadata figures = ImageIntegration::frameMetadataFromProbe(
        probed->metadata, FrameType::Light, result.dimensions.width(), result.dimensions.height(), QFileInfo(trimmed));
    const double exposureSeconds = figures.exposureSeconds();
    if (std::isfinite(exposureSeconds) && exposureSeconds > 0.0)
    {
        result.integrationSeconds = exposureSeconds;
    }

    return result;
}

QString contextWithActualImageResolution(const QString& context, const QString& path)
{
    const QString resolution = resolutionTextForDimensions(masterFileMetadataForPath(path).dimensions);
    if (resolution.isEmpty())
    {
        return context.trimmed();
    }

    QStringList lines = context.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString resolutionLine = SummaryTab::tr("Resolution: %1").arg(resolution);

    bool replaced = false;
    for (QString& line : lines)
    {
        if (line.trimmed().startsWith(QStringLiteral("Resolution:"), Qt::CaseInsensitive))
        {
            line = resolutionLine;
            replaced = true;
        }
    }
    if (!replaced)
    {
        lines << resolutionLine;
    }

    return lines.join(QStringLiteral("\n"));
}

[[nodiscard]] std::optional<double> parsePositiveSecondsText(QString text)
{
    text = text.trimmed().toLower();
    if (text.isEmpty() || text == QStringLiteral("-") || text == QStringLiteral("varies"))
    {
        return std::nullopt;
    }

    const QStringList suffixes = {QStringLiteral("seconds"), QStringLiteral("second"), QStringLiteral("secs"),
                                  QStringLiteral("sec"), QStringLiteral("s")};
    for (const QString& suffix : suffixes)
    {
        if (text.endsWith(suffix))
        {
            text.chop(suffix.size());
            text = text.trimmed();
            break;
        }
    }

    bool ok = false;
    const double seconds = text.toDouble(&ok);
    if (!ok || !std::isfinite(seconds) || seconds <= 0.0)
    {
        return std::nullopt;
    }
    return seconds;
}

[[nodiscard]] double fallbackIntegrationSecondsForGroup(const QModelIndex& index)
{
    const auto exposureSeconds =
        parsePositiveSecondsText(index.data(WorkflowHistoryModel::ExposureRole).toString().trimmed());
    const int frameCount = index.data(WorkflowHistoryModel::FrameCountRole).toInt();
    if (!exposureSeconds.has_value() || frameCount <= 0)
    {
        return 0.0;
    }
    return *exposureSeconds * static_cast<double>(frameCount);
}

QString normalizationModeLabel(int mode)
{
    switch (static_cast<NormalizationMode>(mode))
    {
    case NormalizationMode::None:
        return QStringLiteral("None");
    case NormalizationMode::Global:
        return QStringLiteral("Global");
    case NormalizationMode::Local:
        return QStringLiteral("Local");
    }
    return QStringLiteral("Unknown");
}

QString globalNormalizationMethodLabel(int mode)
{
    switch (static_cast<GlobalNormalizationMethod>(mode))
    {
    case GlobalNormalizationMethod::Additive:
        return QStringLiteral("Additive");
    case GlobalNormalizationMethod::Multiplicative:
        return QStringLiteral("Multiplicative");
    case GlobalNormalizationMethod::AdditiveScaling:
        return QStringLiteral("Additive+Scale");
    case GlobalNormalizationMethod::MultiplicativeScaling:
        return QStringLiteral("Multiplicative+Scale");
    }
    return QStringLiteral("Additive+Scale");
}

QString scaleEstimatorLabel(int method)
{
    switch (static_cast<ScaleEstimatorMethod>(method))
    {
    case ScaleEstimatorMethod::MAD:
        return QStringLiteral("MAD");
    case ScaleEstimatorMethod::IKSS:
        return QStringLiteral("IKSS");
    }
    return QStringLiteral("MAD");
}

QString formatFloatCompact(double value, int decimals = 2)
{
    QString s = QString::number(value, 'f', decimals);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.')))
    {
        s.chop(1);
    }
    return s;
}

bool isAverageStackMethod(const QString& method)
{
    const QString normalized = method.trimmed().toLower();
    return normalized.contains(QStringLiteral("average")) || normalized == QStringLiteral("mean");
}

bool isWeightingEnabled(const QString& weighting)
{
    const QString normalized = weighting.trimmed().toLower();
    return !normalized.isEmpty() && normalized != QStringLiteral("none") && normalized != QStringLiteral("off") &&
           normalized != QStringLiteral("disabled");
}

QString summarizeValues(const QStringList& values)
{
    if (values.isEmpty())
    {
        return QStringLiteral("-");
    }
    const QString first = values.front();
    for (const auto& v : values)
    {
        if (v != first)
        {
            return QStringLiteral("Varies");
        }
    }
    return first;
}

QString summaryValueOrVaries(const QStringList& values)
{
    return summarizeValues(values);
}

struct BreakdownStats
{
    double seconds = 0.0;
    int frames = 0;
};

struct BreakdownText
{
    QString text;
    QString tooltip;
};

QString formatCountedDuration(const BreakdownStats& stats)
{
    const QString duration = formatDuration(stats.seconds);
    if (stats.frames <= 0)
    {
        return duration;
    }
    return SummaryTab::tr("%1 (%2 frames)").arg(duration).arg(stats.frames);
}

BreakdownText formatBreakdownList(const QMap<QString, BreakdownStats>& statsByKey,
                                  const QMap<QString, QString>& displayNameByKey, const QString& noun, int maxLines = 3)
{
    if (statsByKey.isEmpty())
    {
        return {QStringLiteral("-"), QString()};
    }

    struct Item
    {
        QString key;
        QString name;
        BreakdownStats stats;
    };
    std::vector<Item> items;
    items.reserve(static_cast<size_t>(statsByKey.size()));
    for (auto it = statsByKey.cbegin(); it != statsByKey.cend(); ++it)
    {
        const QString key = it.key();
        const QString name = displayNameByKey.value(key, key);
        items.push_back(Item{key, name, it.value()});
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.stats.seconds == b.stats.seconds)
        {
            return a.stats.frames > b.stats.frames;
        }
        return a.stats.seconds > b.stats.seconds;
    });

    BreakdownText out;
    if (items.size() == 1)
    {
        out.text = items.front().name;
        out.tooltip = SummaryTab::tr("%1 — %2").arg(items.front().name, formatCountedDuration(items.front().stats));
        return out;
    }

    out.text = SummaryTab::tr("%1 %2").arg(items.size()).arg(noun);
    out.text += QStringLiteral("\n");

    const int shown = (std::min<int>)(maxLines, static_cast<int>(items.size()));
    for (int i = 0; i < shown; ++i)
    {
        out.text +=
            QStringLiteral("• %1 — %2")
                .arg(items[static_cast<size_t>(i)].name, formatCountedDuration(items[static_cast<size_t>(i)].stats));
        if (i + 1 < shown)
        {
            out.text += QStringLiteral("\n");
        }
    }
    if (static_cast<int>(items.size()) > shown)
    {
        out.text += QStringLiteral("\n");
        out.text += SummaryTab::tr("… +%1 more").arg(static_cast<int>(items.size()) - shown);
    }

    QStringList tooltipLines;
    tooltipLines.reserve(static_cast<int>(items.size()));
    for (const auto& item : items)
    {
        tooltipLines << SummaryTab::tr("%1 — %2").arg(item.name, formatCountedDuration(item.stats));
    }
    out.tooltip = tooltipLines.join(QStringLiteral("\n"));
    return out;
}

QString normalizeKey(const QString& value)
{
    return value.trimmed().toCaseFolded();
}

QString cameraSetupLabel(const FrameInfo& frame)
{
    const QString readout = frame.metadata.readoutMode().trimmed();
    const QString resolution = frame.metadata.resolutionString().trimmed();
    const std::optional<int> gain = frame.metadata.isoGain();
    const std::optional<int> offset = frame.metadata.offset();

    QStringList parts;
    if (!readout.isEmpty())
    {
        parts << readout;
    }
    if (!resolution.isEmpty())
    {
        parts << resolution;
    }
    if (gain.has_value())
    {
        parts << SummaryTab::tr("Gain %1").arg(*gain);
    }
    if (offset.has_value())
    {
        parts << SummaryTab::tr("Offset %1").arg(*offset);
    }
    return parts.join(QStringLiteral(" · "));
}

BreakdownText formatFilterBreakdown(const QMap<QString, BreakdownStats>& perFilter, int maxLines = 4)
{
    if (perFilter.isEmpty())
    {
        return {QStringLiteral("-"), QString()};
    }

    struct FilterItem
    {
        QString name;
        BreakdownStats stats;
    };
    std::vector<FilterItem> items;
    items.reserve(static_cast<size_t>(perFilter.size()));
    for (auto it = perFilter.cbegin(); it != perFilter.cend(); ++it)
    {
        items.push_back({it.key(), it.value()});
    }
    std::sort(items.begin(), items.end(),
              [](const FilterItem& a, const FilterItem& b) { return a.stats.seconds > b.stats.seconds; });

    BreakdownText out;
    const int shown = (std::min<int>)(maxLines, static_cast<int>(items.size()));
    QStringList lines;
    for (int i = 0; i < shown; ++i)
    {
        lines << SummaryTab::tr("%1: %2").arg(items[static_cast<size_t>(i)].name,
                                              formatDuration(items[static_cast<size_t>(i)].stats.seconds));
    }
    if (static_cast<int>(items.size()) > shown)
    {
        lines << SummaryTab::tr("… +%1 more").arg(static_cast<int>(items.size()) - shown);
    }
    out.text = lines.join(QStringLiteral("\n"));

    QStringList tooltipLines;
    for (const auto& item : items)
    {
        tooltipLines << SummaryTab::tr("%1: %2 (%3 frames)")
                            .arg(item.name)
                            .arg(formatDuration(item.stats.seconds))
                            .arg(item.stats.frames);
    }
    out.tooltip = tooltipLines.join(QStringLiteral("\n"));
    return out;
}

QString buildRunSummaryTooltip(const QJsonObject& manifest)
{
    QStringList lines;

    const QJsonArray lights_cal = manifest.value(QStringLiteral("lightsCalibration")).toArray();
    if (lights_cal.isEmpty())
    {
        lines << QStringLiteral("Calibration: none");
    }
    else
    {
        QStringList ccd_modes;
        QStringList cosmetic;
        QStringList sigma_hot;
        QStringList sigma_cold;
        QStringList dark_opt;
        QStringList dark_mult;
        QStringList flat_auto;
        QStringList pedestal;

        for (const auto& v : lights_cal)
        {
            const QJsonObject settings = v.toObject().value(QStringLiteral("settings")).toObject();
            ccd_modes << boolLabel(settings.value(QStringLiteral("subtractBiasFromDark")).toBool(),
                                   QStringLiteral("On"), QStringLiteral("Off"));
            cosmetic << boolLabel(settings.value(QStringLiteral("cosmeticCorrection")).toBool(), QStringLiteral("On"),
                                  QStringLiteral("Off"));
            sigma_hot << QString::number(settings.value(QStringLiteral("sigmaHot")).toDouble(), 'f', 2);
            sigma_cold << QString::number(settings.value(QStringLiteral("sigmaCold")).toDouble(), 'f', 2);
            dark_opt << boolLabel(settings.value(QStringLiteral("darkOptimization")).toBool(), QStringLiteral("On"),
                                  QStringLiteral("Off"));
            dark_mult << QString::number(settings.value(QStringLiteral("darkMultiplicationFactor")).toDouble(), 'f', 2);
            flat_auto << boolLabel(settings.value(QStringLiteral("flatAutoLevel")).toBool(), QStringLiteral("On"),
                                   QStringLiteral("Off"));
            pedestal << QString::number(settings.value(QStringLiteral("pedestal")).toDouble(), 'f', 2);
        }

        lines << QStringLiteral("Calibration: %1 lights | CCD: %2 | Cosmetic: %3 | SigmaHot/Cold: %4/%5 | DarkOpt: %6 "
                                "| DarkMult: %7 | FlatAuto: %8 | Pedestal: %9")
                     .arg(lights_cal.size())
                     .arg(summaryValueOrVaries(ccd_modes))
                     .arg(summaryValueOrVaries(cosmetic))
                     .arg(summaryValueOrVaries(sigma_hot))
                     .arg(summaryValueOrVaries(sigma_cold))
                     .arg(summaryValueOrVaries(dark_opt))
                     .arg(summaryValueOrVaries(dark_mult))
                     .arg(summaryValueOrVaries(flat_auto))
                     .arg(summaryValueOrVaries(pedestal));
    }

    const QJsonArray reg_groups = manifest.value(QStringLiteral("registrationGroups")).toArray();
    if (reg_groups.isEmpty())
    {
        lines << QStringLiteral("Registration: none");
    }
    else
    {
        QStringList alignmentModes;
        QStringList transform;
        QStringList interpolation;
        QStringList cfa_pattern;
        QStringList debayer;
        for (const auto& v : reg_groups)
        {
            const QJsonObject settings = v.toObject().value(QStringLiteral("settings")).toObject();
            alignmentModes << settings.value(QStringLiteral("alignmentMode")).toString();
            const QString method = settings.value(QStringLiteral("registrationMethod")).toString();
            if (method.trimmed().compare(QStringLiteral("phase_correlation"), Qt::CaseInsensitive) == 0)
            {
                transform << QStringLiteral("Phase Correlation");
            }
            else if (method.trimmed().compare(QStringLiteral("comet_tracking"), Qt::CaseInsensitive) == 0 ||
                     method.trimmed().compare(QStringLiteral("comet tracking"), Qt::CaseInsensitive) == 0)
            {
                transform << QStringLiteral("Comet Tracking");
            }
            else if (method.trimmed().compare(QStringLiteral("manual_points"), Qt::CaseInsensitive) == 0 ||
                     method.trimmed().compare(QStringLiteral("manual points"), Qt::CaseInsensitive) == 0)
            {
                transform << QStringLiteral("Manual Points");
            }
            else
            {
                transform << settings.value(QStringLiteral("transformation")).toString();
            }
            interpolation << settings.value(QStringLiteral("interpolation")).toString();
            cfa_pattern << settings.value(QStringLiteral("cfaPattern")).toString();
            debayer << settings.value(QStringLiteral("debayerMethod")).toString();
        }

        bool anyCfa = false;
        for (const auto& v : cfa_pattern)
        {
            if (!v.trimmed().isEmpty())
            {
                anyCfa = true;
                break;
            }
        }

        if (!anyCfa)
        {
            lines << QStringLiteral("Registration: %1 groups | Align: %2 | Transform: %3 | Interp: %4")
                         .arg(reg_groups.size())
                         .arg(summaryValueOrVaries(alignmentModes))
                         .arg(summaryValueOrVaries(transform))
                         .arg(summaryValueOrVaries(interpolation));
        }
        else
        {
            lines << QStringLiteral(
                         "Registration: %1 groups | Align: %2 | Transform: %3 | Interp: %4 | CFA: %5 | Debayer: %6")
                         .arg(reg_groups.size())
                         .arg(summaryValueOrVaries(alignmentModes))
                         .arg(summaryValueOrVaries(transform))
                         .arg(summaryValueOrVaries(interpolation))
                         .arg(summaryValueOrVaries(cfa_pattern))
                         .arg(summaryValueOrVaries(debayer));
        }
    }

    const QJsonArray int_groups = manifest.value(QStringLiteral("integrationGroups")).toArray();
    if (int_groups.isEmpty())
    {
        lines << QStringLiteral("Stacking: none");
    }
    else
    {
        QStringList stack_method;
        QStringList rejection;
        QStringList sigma_low;
        QStringList sigma_high;
        QStringList weighting;
        QStringList star_scale;
        QStringList alpha;
        QStringList min_floor;
        QStringList per_channel_fwhm;
        QStringList norm_mode;
        QStringList global_norm_method;
        QStringList scale_estimator;
        QStringList local_grid;
        QStringList local_ref;
        QStringList local_scale;
        QStringList drizzle;
        QStringList drizzle_details;
        for (const auto& v : int_groups)
        {
            const QJsonObject stacking = v.toObject().value(QStringLiteral("stacking")).toObject();
            const QString stackMethod = stacking.value(QStringLiteral("stackMethod")).toString();
            stack_method << stackMethod;

            if (isAverageStackMethod(stackMethod))
            {
                rejection << stacking.value(QStringLiteral("rejectionMethod")).toString();
                sigma_low << formatFloatCompact(stacking.value(QStringLiteral("sigmaLow")).toDouble(), 2);
                sigma_high << formatFloatCompact(stacking.value(QStringLiteral("sigmaHigh")).toDouble(), 2);
            }

            const QString weightingMode = stacking.value(QStringLiteral("weighting")).toString();
            weighting << (isWeightingEnabled(weightingMode) ? weightingMode : QStringLiteral("Off"));
            star_scale << stacking.value(QStringLiteral("starCountScaling")).toString();
            alpha << formatFloatCompact(stacking.value(QStringLiteral("exponentialAlpha")).toDouble(), 2);
            min_floor << formatFloatCompact(stacking.value(QStringLiteral("minWeightFloor")).toDouble(), 2);
            per_channel_fwhm << boolLabel(stacking.value(QStringLiteral("perChannelFwhmWeighting")).toBool(),
                                          QStringLiteral("On"), QStringLiteral("Off"));

            norm_mode << normalizationModeLabel(stacking.value(QStringLiteral("normalizationMode")).toInt());
            global_norm_method << globalNormalizationMethodLabel(
                stacking.value(QStringLiteral("globalNormalizationMethod")).toInt());
            scale_estimator << scaleEstimatorLabel(stacking.value(QStringLiteral("scaleEstimator")).toInt());

            const QJsonObject local_obj = stacking.value(QStringLiteral("localNormalization")).toObject();
            local_grid << QString::number(local_obj.value(QStringLiteral("gridSize")).toInt());
            local_ref << local_obj.value(QStringLiteral("referenceStrategy")).toString();
            local_scale << boolLabel(local_obj.value(QStringLiteral("localScaleEnabled")).toBool(),
                                     QStringLiteral("On"), QStringLiteral("Off"));

            const QJsonObject drizzle_obj = stacking.value(QStringLiteral("drizzle")).toObject();
            const bool drizzleEnabled = drizzle_obj.value(QStringLiteral("enabled")).toBool();
            drizzle << boolLabel(drizzleEnabled, QStringLiteral("On"), QStringLiteral("Off"));
            if (drizzleEnabled)
            {
                drizzle_details
                    << QStringLiteral("%1x, pf=%2, %3")
                           .arg(formatFloatCompact(drizzle_obj.value(QStringLiteral("scale")).toDouble(), 2))
                           .arg(formatFloatCompact(drizzle_obj.value(QStringLiteral("pixelFraction")).toDouble(), 2))
                           .arg(drizzle_obj.value(QStringLiteral("kernel")).toString());
            }
            else
            {
                drizzle_details << QStringLiteral("-");
            }
        }

        const QString methodSummary = summaryValueOrVaries(stack_method);
        const bool showRejection = !rejection.isEmpty();

        QString stackingLine = QStringLiteral("Stacking: %1 groups | Method: %2 | Normalization: %3 | Drizzle: %4")
                                   .arg(int_groups.size())
                                   .arg(methodSummary)
                                   .arg(summaryValueOrVaries(norm_mode))
                                   .arg(summaryValueOrVaries(drizzle));

        if (showRejection)
        {
            stackingLine += QStringLiteral(" | Rejection(avg): %1 (%2/%3)")
                                .arg(summaryValueOrVaries(rejection))
                                .arg(summaryValueOrVaries(sigma_low))
                                .arg(summaryValueOrVaries(sigma_high));
        }

        lines << stackingLine;

        QStringList detailParts;
        detailParts << QStringLiteral("Weighting: %1").arg(summaryValueOrVaries(weighting));
        if (isWeightingEnabled(summaryValueOrVaries(weighting)))
        {
            const QString sc = summaryValueOrVaries(star_scale);
            if (!sc.trimmed().isEmpty())
            {
                detailParts << QStringLiteral("StarScale: %1").arg(sc);
            }
            detailParts << QStringLiteral("Alpha: %1").arg(summaryValueOrVaries(alpha));
            detailParts << QStringLiteral("Floor: %1").arg(summaryValueOrVaries(min_floor));
            detailParts << QStringLiteral("PerChanFWHM: %1").arg(summaryValueOrVaries(per_channel_fwhm));
        }

        const QString norm = summaryValueOrVaries(norm_mode);
        if (norm == QStringLiteral("Global"))
        {
            detailParts << QStringLiteral("GlobalNorm: %1").arg(summaryValueOrVaries(global_norm_method));
            detailParts << QStringLiteral("ScaleEst: %1").arg(summaryValueOrVaries(scale_estimator));
        }
        else if (norm == QStringLiteral("Local"))
        {
            detailParts << QStringLiteral("LocalNorm: grid=%1, ref=%2, localScale=%3")
                               .arg(summaryValueOrVaries(local_grid))
                               .arg(summaryValueOrVaries(local_ref))
                               .arg(summaryValueOrVaries(local_scale));
            detailParts << QStringLiteral("GlobalOnTop: Additive With Scaling");
            detailParts << QStringLiteral("ScaleEst: %1").arg(summaryValueOrVaries(scale_estimator));
        }

        if (summaryValueOrVaries(drizzle) == QStringLiteral("On"))
        {
            detailParts << QStringLiteral("Drizzle: %1").arg(summaryValueOrVaries(drizzle_details));
        }

        if (!detailParts.isEmpty())
        {
            lines << detailParts.join(QStringLiteral(" | "));
        }
    }

    return lines.join(QStringLiteral("\n"));
}

QString buildRunSummaryLine(const QJsonObject& manifest)
{
    const QJsonArray int_groups = manifest.value(QStringLiteral("integrationGroups")).toArray();
    if (int_groups.isEmpty())
    {
        return QString();
    }

    QStringList stack_method;
    QStringList rejection;
    QStringList sigma_low;
    QStringList sigma_high;
    QStringList weighting;
    QStringList alpha;
    QStringList min_floor;
    QStringList norm_mode;
    QStringList drizzle;
    QStringList drizzle_compact;

    for (const auto& v : int_groups)
    {
        const QJsonObject stacking = v.toObject().value(QStringLiteral("stacking")).toObject();
        const QString stackMethod = stacking.value(QStringLiteral("stackMethod")).toString();
        stack_method << stackMethod;

        if (isAverageStackMethod(stackMethod))
        {
            rejection << stacking.value(QStringLiteral("rejectionMethod")).toString();
            sigma_low << formatFloatCompact(stacking.value(QStringLiteral("sigmaLow")).toDouble(), 2);
            sigma_high << formatFloatCompact(stacking.value(QStringLiteral("sigmaHigh")).toDouble(), 2);
        }

        const QString weightingMode = stacking.value(QStringLiteral("weighting")).toString();
        weighting << (isWeightingEnabled(weightingMode) ? weightingMode : QStringLiteral("Off"));
        alpha << formatFloatCompact(stacking.value(QStringLiteral("exponentialAlpha")).toDouble(), 2);
        min_floor << formatFloatCompact(stacking.value(QStringLiteral("minWeightFloor")).toDouble(), 2);
        norm_mode << normalizationModeLabel(stacking.value(QStringLiteral("normalizationMode")).toInt());

        const QJsonObject drizzle_obj = stacking.value(QStringLiteral("drizzle")).toObject();
        const bool drizzleEnabled = drizzle_obj.value(QStringLiteral("enabled")).toBool();
        drizzle << boolLabel(drizzleEnabled, QStringLiteral("On"), QStringLiteral("Off"));
        if (drizzleEnabled)
        {
            drizzle_compact << QStringLiteral("%1x,pf%2")
                                   .arg(formatFloatCompact(drizzle_obj.value(QStringLiteral("scale")).toDouble(), 2))
                                   .arg(formatFloatCompact(
                                       drizzle_obj.value(QStringLiteral("pixelFraction")).toDouble(), 2));
        }
    }

    const QString methodSummary = summaryValueOrVaries(stack_method);
    const bool showRejection = !rejection.isEmpty();

    QStringList parts;
    parts << QStringLiteral("Stack: %1").arg(methodSummary);
    if (showRejection)
    {
        parts << QStringLiteral("Rej(avg): %1 (%2/%3)")
                     .arg(summaryValueOrVaries(rejection))
                     .arg(summaryValueOrVaries(sigma_low))
                     .arg(summaryValueOrVaries(sigma_high));
    }

    const QString weightSummary = summaryValueOrVaries(weighting);
    if (isWeightingEnabled(weightSummary))
    {
        parts << QStringLiteral("Weight: %1 (α=%2, floor=%3)")
                     .arg(weightSummary)
                     .arg(summaryValueOrVaries(alpha))
                     .arg(summaryValueOrVaries(min_floor));
    }
    else
    {
        parts << QStringLiteral("Weight: Off");
    }

    parts << QStringLiteral("Norm: %1").arg(summaryValueOrVaries(norm_mode));

    const QString drizzleSummary = summaryValueOrVaries(drizzle);
    if (drizzleSummary == QStringLiteral("On"))
    {
        parts << QStringLiteral("Drizzle: On (%1)").arg(summaryValueOrVaries(drizzle_compact));
    }
    else
    {
        parts << QStringLiteral("Drizzle: Off");
    }

    return parts.join(QStringLiteral(" \u00B7 "));
}

QString formatDuration(double seconds)
{
    if (seconds <= 0.0)
    {
        return QStringLiteral("-");
    }

    const int total = static_cast<int>(std::round(seconds));
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;

    if (hours > 0)
    {
        return SummaryTab::tr("%1h %2m").arg(hours).arg(minutes);
    }
    if (minutes > 0)
    {
        return SummaryTab::tr("%1m %2s").arg(minutes).arg(secs);
    }
    return SummaryTab::tr("%1s").arg(secs);
}

QString formatOutputBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024)
        return QString::number(kb, 'f', 1) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024)
        return QString::number(mb, 'f', 1) + " MB";
    double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + " GB";
}

QString summarizeDirectories(const QStringList& dirs, int maxCount = 3)
{
    if (dirs.isEmpty())
    {
        return QStringLiteral("-");
    }

    QStringList unique = dirs;
    unique.removeDuplicates();
    if (unique.size() <= maxCount)
    {
        return unique.join(QStringLiteral("; "));
    }

    const QStringList head = unique.mid(0, maxCount);
    return SummaryTab::tr("%1; +%2 more").arg(head.join(QStringLiteral("; "))).arg(unique.size() - maxCount);
}

Qt::CaseSensitivity pathCaseSensitivity()
{
#if defined(Q_OS_WIN) || defined(Q_OS_DARWIN)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString canonicalOrAbsoluteDirectoryPath(const QString& path)
{
    QFileInfo info(path.trimmed());
    QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty())
    {
        resolved = info.absoluteFilePath();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(resolved));
}

bool isStrictChildDirectory(const QString& childPath, const QString& parentPath)
{
    const QString child = canonicalOrAbsoluteDirectoryPath(childPath);
    const QString parent = canonicalOrAbsoluteDirectoryPath(parentPath);
    if (child.isEmpty() || parent.isEmpty())
    {
        return false;
    }

    const Qt::CaseSensitivity sensitivity = pathCaseSensitivity();
    if (child.compare(parent, sensitivity) == 0)
    {
        return false;
    }

    const QString parentPrefix = parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/');
    return child.startsWith(parentPrefix, sensitivity);
}

} // namespace

class SummaryTab::Impl
{
public:
    SummaryTab* q;
    an::ui::imageintegration::SummaryTabUiElements ui;
    IntegrationDataModel* model;
    QTimer* debounceTimer;
    QTimer* notesTimer;
    QString lastNotesDir;
    bool loadingNotes = false;
    WorkflowHistoryModel* historyModel = nullptr;
    WorkflowHistoryDelegate* historyDelegate = nullptr;
    RunHistoryThumbnailCache* thumbnailCache = nullptr;
    QTreeView* historyView = nullptr;

    explicit Impl(SummaryTab* parent, IntegrationDataModel* m) : q(parent), model(m)
    {
        ui = an::ui::imageintegration::buildSummaryTabUi(q);
        setupFonts();
        styleExecuteButton();
        applyBackground();
        configureScrollArea();
        setupRunHistoryView();
        setupDebounceTimer();
        setupNotesTimer();
        setupConnections();
        updateAll();
    }

    void setupFonts()
    {
        const auto& typography = an::ui::tokens().typography;
        auto setTitleFont = [&](QLabel* label, const QFont& tokenFont) {
            if (!label)
            {
                return;
            }
            label->setFont(tokenFont);
        };

        setTitleFont(ui.lblStatusTitle, typography.heading1);
        setTitleFont(ui.lblIntegrationSummaryTitle, typography.heading2);
        setTitleFont(ui.lblSessionNotesTitle, typography.heading2);
        setTitleFont(ui.lblRunHistoryTitle, typography.heading2);
    }

    void styleExecuteButton()
    {
        if (!ui.btnExecute)
        {
            return;
        }

        // Add icon
        ui.btnExecute->setIcon(summaryIcon(QStringLiteral("play")));

        // P1 #5: Visually distinct style (Primary Action)
        // Note: 'default' and 'autoDefault' are set in .ui
        ui.btnExecute->setProperty("anButtonVariant", QStringLiteral("primary"));
        ui.btnExecute->setFont(an::ui::tokens().typography.heading3);
    }

    void configureScrollArea()
    {
        // The entire content is now wrapped in a single scroll area.
        // Configure the scroll area and its contents for proper background rendering.
        if (ui.scrollArea)
        {
            ui.scrollArea->setFrameShape(QFrame::NoFrame);
            // Ensure scroll area contents inherit the background
            if (ui.scrollAreaContents)
            {
                ui.scrollAreaContents->setAutoFillBackground(true);
            }
        }
    }

    void applyBackground(QWidget* widget = nullptr, bool useBase = false)
    {
        QWidget* target = widget ? widget : q;
        if (!target)
        {
            return;
        }

        // Let the QTabWidget pane draw the page background so the surrounding "gutter"
        // (matching the tab header) remains visible. We only paint backgrounds for inner
        // containers (group boxes / frames / scroll contents).
        if (target == q)
        {
            target->setAutoFillBackground(false);
            target->setAttribute(Qt::WA_StyledBackground, false);
        }
        else
        {
            // Ensure we always repaint cleared background regions when child widgets move/resize
            // (prevents "overlap trails" during interactive window resizing).
            target->setAutoFillBackground(true);
            target->setAttribute(Qt::WA_StyledBackground, true);
        }

        QWidget* palSource = target->parentWidget() ? target->parentWidget() : q;
        if (palSource && target != q)
        {
            QPalette pal = target->palette();
            const QPalette sourcePal = qApp ? qApp->palette() : palSource->palette();
            const QColor bgColor = useBase ? sourcePal.color(QPalette::Base) : sourcePal.color(QPalette::Window);
            pal.setColor(QPalette::Window, bgColor);
            target->setPalette(pal);
        }

        if (target == q)
        {
            if (ui.scrollAreaContents)
            {
                applyBackground(ui.scrollAreaContents);
            }
            if (ui.scrollArea)
            {
                applyBackground(ui.scrollArea);
                if (ui.scrollArea->viewport())
                {
                    applyBackground(ui.scrollArea->viewport());
                }
            }
            if (ui.statusCardFrame)
            {
                applyBackground(ui.statusCardFrame);
            }
            if (ui.statusIssuesWidget)
            {
                applyBackground(ui.statusIssuesWidget);
            }
            if (ui.integrationSummaryFrame)
            {
                applyBackground(ui.integrationSummaryFrame);
            }
            if (ui.integrationReportWidget)
            {
                applyBackground(ui.integrationReportWidget);
            }
            if (ui.sessionNotesFrame)
            {
                applyBackground(ui.sessionNotesFrame);
            }
            if (ui.runHistoryFrame)
            {
                applyBackground(ui.runHistoryFrame);
            }
            if (ui.resourceStatusWidget)
            {
                applyBackground(ui.resourceStatusWidget);
            }
        }
    }

    QString masterLabelForGroupPath(const QModelIndex& index, const QString& path) const
    {
        QString runLabel;
        if (historyModel)
        {
            const QModelIndex workflowIndex = historyModel->parent(index);
            const QDateTime timestamp = workflowIndex.data(WorkflowHistoryModel::TimestampRole).toDateTime();
            if (timestamp.isValid())
            {
                runLabel = timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            }
            else
            {
                runLabel = workflowIndex.data(WorkflowHistoryModel::ExecutionIdRole).toString().trimmed();
            }
        }

        const QString filename = index.data(WorkflowHistoryModel::OutputFilenameRole).toString().trimmed().isEmpty()
                                     ? QFileInfo(path).fileName()
                                     : index.data(WorkflowHistoryModel::OutputFilenameRole).toString().trimmed();
        const QString groupLabel = index.data(WorkflowHistoryModel::GroupKeyRole).toString().trimmed();

        QStringList pieces;
        if (!runLabel.isEmpty())
        {
            pieces << runLabel;
        }

        if (!groupLabel.isEmpty())
        {
            pieces << groupLabel;
        }
        if (!filename.isEmpty())
        {
            pieces << filename;
        }

        return pieces.isEmpty() ? QFileInfo(path).fileName() : pieces.join(QStringLiteral(" - "));
    }

    bool appendExistingMasterPath(const QString& path, const QString& label, const QString& context,
                                  QSet<QString>& seenPaths, ComparableMasterSelection& selection) const
    {
        const QString trimmed = path.trimmed();
        if (trimmed.isEmpty())
        {
            return false;
        }

        const QFileInfo info(trimmed);
        if (!info.exists() || !info.isFile())
        {
            return false;
        }

        QString dedupeKey = info.canonicalFilePath();
        if (dedupeKey.isEmpty())
        {
            dedupeKey = info.absoluteFilePath();
        }

        if (seenPaths.contains(dedupeKey))
        {
            return false;
        }

        seenPaths.insert(dedupeKey);
        selection.paths.append(trimmed);
        selection.labels.append(label.trimmed().isEmpty() ? QFileInfo(trimmed).fileName() : label.trimmed());
        selection.contexts.append(contextWithActualImageResolution(context, trimmed));
        return true;
    }

    QModelIndex mostRecentCompatibleGroupBefore(const QModelIndex& index) const
    {
        if (!historyModel || !index.isValid())
        {
            return {};
        }

        const QModelIndex workflowIndex = historyModel->parent(index);
        const QDateTime sourceTimestamp = workflowIndex.data(WorkflowHistoryModel::TimestampRole).toDateTime();
        QModelIndex bestIndex;
        QDateTime bestTimestamp;
        int bestScore = -1;

        const int workflowCount = historyModel->rowCount();
        for (int workflowRow = 0; workflowRow < workflowCount; ++workflowRow)
        {
            const QModelIndex candidateWorkflowIndex = historyModel->index(workflowRow, 0);
            const QDateTime candidateTimestamp =
                candidateWorkflowIndex.data(WorkflowHistoryModel::TimestampRole).toDateTime();
            if (sourceTimestamp.isValid() && (!candidateTimestamp.isValid() || candidateTimestamp >= sourceTimestamp))
            {
                continue;
            }

            const int groupCount = historyModel->rowCount(candidateWorkflowIndex);
            for (int groupRow = 0; groupRow < groupCount; ++groupRow)
            {
                const QModelIndex candidate = historyModel->index(groupRow, 0, candidateWorkflowIndex);
                if (candidate == index || !historyGroupsCanCompare(index, candidate))
                {
                    continue;
                }

                const int score = historyContextMatchScore(index, candidate);
                if (score < bestScore)
                {
                    continue;
                }
                if (score == bestScore && bestTimestamp.isValid() && candidateTimestamp.isValid() &&
                    candidateTimestamp <= bestTimestamp)
                {
                    continue;
                }

                bestIndex = candidate;
                bestTimestamp = candidateTimestamp;
                bestScore = score;
            }
        }

        return bestIndex;
    }

    QString archivedMasterContextForGroup(const QModelIndex& index) const
    {
        const QModelIndex originalGroup = mostRecentCompatibleGroupBefore(index);
        const QString originalContext =
            originalGroup.data(WorkflowHistoryModel::MasterContextRole).toString().trimmed();
        if (!originalContext.isEmpty())
        {
            return originalContext;
        }
        return SummaryTab::tr("Original workflow context is not available.");
    }

    QVariantMap historyRowForGroupPath(const QModelIndex& index, const QString& path, const QString& label,
                                       bool archived) const
    {
        QVariantMap row;
        const QString cleanPath = path.trimmed();
        row.insert(QStringLiteral("label"),
                   label.trimmed().isEmpty() ? QFileInfo(cleanPath).fileName() : label.trimmed());
        row.insert(QStringLiteral("path"), cleanPath);
        row.insert(QStringLiteral("archived"), archived);

        if (!historyModel || !index.isValid())
        {
            return row;
        }

        const QModelIndex workflowIndex = historyModel->parent(index);
        const QDateTime timestamp = workflowIndex.data(WorkflowHistoryModel::TimestampRole).toDateTime();
        row.insert(QStringLiteral("executedAt"), timestamp);
        row.insert(QStringLiteral("executedAtText"),
                   timestamp.isValid() ? timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                       : QString());
        row.insert(QStringLiteral("executionId"), workflowIndex.data(WorkflowHistoryModel::ExecutionIdRole));
        row.insert(QStringLiteral("mode"), workflowIndex.data(WorkflowHistoryModel::ModeRole));
        row.insert(QStringLiteral("status"), index.data(WorkflowHistoryModel::StatusRole));

        row.insert(QStringLiteral("frameCount"), index.data(WorkflowHistoryModel::FrameCountRole));
        row.insert(QStringLiteral("groupKey"), index.data(WorkflowHistoryModel::GroupKeyRole));
        row.insert(QStringLiteral("rawGroupKey"), index.data(WorkflowHistoryModel::RawGroupKeyRole));
        row.insert(QStringLiteral("outputFilename"), index.data(WorkflowHistoryModel::OutputFilenameRole));
        row.insert(QStringLiteral("filter"), index.data(WorkflowHistoryModel::FilterRole));
        row.insert(QStringLiteral("object"), index.data(WorkflowHistoryModel::ObjectRole));
        const MasterFileMetadata masterMetadata = masterFileMetadataForPath(cleanPath);
        const QString actualResolution = resolutionTextForDimensions(masterMetadata.dimensions);
        row.insert(QStringLiteral("resolution"), actualResolution.isEmpty()
                                                     ? index.data(WorkflowHistoryModel::ResolutionRole).toString()
                                                     : actualResolution);
        const double integrationSeconds = masterMetadata.integrationSeconds > 0.0
                                              ? masterMetadata.integrationSeconds
                                              : fallbackIntegrationSecondsForGroup(index);
        if (integrationSeconds > 0.0)
        {
            row.insert(QStringLiteral("integrationSeconds"), integrationSeconds);
            row.insert(QStringLiteral("integrationTimeText"), formatDuration(integrationSeconds));
        }
        row.insert(QStringLiteral("registrationGroupKey"), index.data(WorkflowHistoryModel::RegistrationGroupKeyRole));
        return row;
    }

    void appendExistingMasterPathsForGroup(const QModelIndex& index, QSet<QString>& seenPaths,
                                           ComparableMasterSelection& selection) const
    {
        const QString outputPath = index.data(WorkflowHistoryModel::OutputPathRole).toString();
        const QString masterContext = index.data(WorkflowHistoryModel::MasterContextRole).toString();
        const QString outputLabel = masterLabelForGroupPath(index, outputPath);
        if (appendExistingMasterPath(outputPath, outputLabel, masterContext, seenPaths, selection))
        {
            selection.historyRows.append(historyRowForGroupPath(index, outputPath, outputLabel, false));
        }

        const QStringList archivedPaths = index.data(WorkflowHistoryModel::ArchivedOutputPathsRole).toStringList();
        for (const QString& archivedPath : archivedPaths)
        {
            const QModelIndex originalGroup = mostRecentCompatibleGroupBefore(index);
            const QModelIndex labelGroup = originalGroup.isValid() ? originalGroup : index;
            const QString archivedLabel = masterLabelForGroupPath(labelGroup, archivedPath);
            if (!appendExistingMasterPath(archivedPath, archivedLabel, archivedMasterContextForGroup(index), seenPaths,
                                          selection))
            {
                continue;
            }

            selection.historyRows.append(historyRowForGroupPath(labelGroup, archivedPath, archivedLabel, true));
        }
    }

    void appendExistingCurrentMasterPathForGroup(const QModelIndex& index, QSet<QString>& seenPaths,
                                                 ComparableMasterSelection& selection) const
    {
        const QString outputPath = index.data(WorkflowHistoryModel::OutputPathRole).toString();
        const QString masterContext = index.data(WorkflowHistoryModel::MasterContextRole).toString();
        const QString outputLabel = masterLabelForGroupPath(index, outputPath);
        if (appendExistingMasterPath(outputPath, outputLabel, masterContext, seenPaths, selection))
        {
            selection.historyRows.append(historyRowForGroupPath(index, outputPath, outputLabel, false));
        }
    }

    ComparableMasterSelection comparableMastersForGroup(const QModelIndex& sourceIndex,
                                                        QString* outGroupKey = nullptr) const
    {
        if (!historyModel || !sourceIndex.isValid() ||
            !sourceIndex.data(WorkflowHistoryModel::IsGroupItemRole).toBool() ||
            sourceIndex.data(WorkflowHistoryModel::StatusRole).toString() != QStringLiteral("success"))
        {
            return {};
        }

        const QString groupKey = sourceIndex.data(WorkflowHistoryModel::GroupKeyRole).toString();
        if (outGroupKey)
        {
            *outGroupKey = groupKey;
        }

        QSet<QString> seenPaths;
        ComparableMasterSelection selection;

        appendExistingCurrentMasterPathForGroup(sourceIndex, seenPaths, selection);

        const int workflowCount = historyModel->rowCount();
        for (int workflowRow = 0; workflowRow < workflowCount; ++workflowRow)
        {
            const QModelIndex workflowIndex = historyModel->index(workflowRow, 0);
            const int groupCount = historyModel->rowCount(workflowIndex);
            for (int groupRow = 0; groupRow < groupCount; ++groupRow)
            {
                const QModelIndex groupIndex = historyModel->index(groupRow, 0, workflowIndex);
                if (!historyGroupsCanCompare(sourceIndex, groupIndex))
                {
                    continue;
                }

                appendExistingMasterPathsForGroup(groupIndex, seenPaths, selection);
            }
        }

        return selection;
    }

    void compareMastersForGroup(const QModelIndex& index)
    {
        QString groupKey;
        const ComparableMasterSelection selection = comparableMastersForGroup(index, &groupKey);
        if (selection.paths.size() < 2)
        {
            process_utils::notifyWarnMessage(
                q, SummaryTab::tr("Compare Masters"),
                SummaryTab::tr("Only one available stacked master was found for %1. Run another integration for the "
                               "same group before comparing.")
                    .arg(groupKey.isEmpty() ? SummaryTab::tr("this group") : groupKey));
            return;
        }

        emit q->compareRequested(selection.paths, selection.labels, selection.contexts, selection.historyRows,
                                 groupKey);
    }

    QString workflowRunDirFromIndex(const QModelIndex& index) const
    {
        if (!index.isValid() || index.data(WorkflowHistoryModel::IsGroupItemRole).toBool())
        {
            return {};
        }

        QString runDir = index.data(WorkflowHistoryModel::RunDirPathRole).toString().trimmed();
        if (runDir.isEmpty())
        {
            const QString manifestPath = index.data(WorkflowHistoryModel::ManifestPathRole).toString().trimmed();
            if (!manifestPath.isEmpty())
            {
                runDir = QFileInfo(manifestPath).absolutePath();
            }
        }

        return runDir;
    }

    QString deleteValidationError(const QString& runDir) const
    {
        if (runDir.trimmed().isEmpty())
        {
            return SummaryTab::tr("The selected workflow archive does not have a valid folder.");
        }

        const QString outputDir = model ? model->outputDirectory().trimmed() : QString();
        if (outputDir.isEmpty())
        {
            return SummaryTab::tr("The output directory is not set, so the archive location cannot be verified.");
        }

        const QFileInfo archiveInfo(runDir);
        if (!archiveInfo.exists() || !archiveInfo.isDir())
        {
            return SummaryTab::tr("The selected workflow archive folder no longer exists.");
        }
        if (archiveInfo.isSymLink())
        {
            return SummaryTab::tr("The selected workflow archive folder is a symbolic link and will not be deleted.");
        }

        const QString historyDir = QDir(outputDir).filePath(QStringLiteral("history"));
        if (!isStrictChildDirectory(runDir, historyDir))
        {
            return SummaryTab::tr(
                "The selected workflow archive is not inside this output directory's history folder.");
        }

        return {};
    }

    void deleteWorkflowArchive(const QString& runDir)
    {
        const QString validationError = deleteValidationError(runDir);
        if (!validationError.isEmpty())
        {
            process_utils::notifyErrorMessageAsync(q, SummaryTab::tr("Delete Workflow Archive"), validationError);
            updateRunHistory();
            return;
        }

        QDir archiveDir(runDir);
        if (!archiveDir.removeRecursively())
        {
            process_utils::notifyErrorMessageAsync(
                q, SummaryTab::tr("Delete Workflow Archive"),
                SummaryTab::tr("AfterNight could not delete the workflow archive folder:\n%1").arg(runDir));
            return;
        }

        updateRunHistory();
    }

    void confirmDeleteWorkflowArchive(const QModelIndex& index)
    {
        const QString runDir = workflowRunDirFromIndex(index);
        const QString validationError = deleteValidationError(runDir);
        if (!validationError.isEmpty())
        {
            process_utils::notifyErrorMessageAsync(q, SummaryTab::tr("Delete Workflow Archive"), validationError);
            updateRunHistory();
            return;
        }

        const QString executionId = index.data(WorkflowHistoryModel::ExecutionIdRole).toString().trimmed();
        const QString subject = executionId.isEmpty() ? QFileInfo(runDir).fileName() : executionId;

        auto* messageBox = new QMessageBox(QMessageBox::Warning, SummaryTab::tr("Delete Workflow Archive"),
                                           SummaryTab::tr("Delete workflow archive \"%1\"?").arg(subject),
                                           QMessageBox::Yes | QMessageBox::Cancel, q);
        messageBox->setAttribute(Qt::WA_DeleteOnClose);
        messageBox->setInformativeText(
            SummaryTab::tr("This will permanently delete the execution folder, including logs, JSON manifests, "
                           "reports, previews, and archived master files. This cannot be undone."));
        messageBox->setDefaultButton(QMessageBox::Cancel);
        messageBox->setEscapeButton(QMessageBox::Cancel);
        if (auto* deleteButton = messageBox->button(QMessageBox::Yes))
        {
            deleteButton->setText(SummaryTab::tr("Delete"));
        }

        QObject::connect(messageBox, &QMessageBox::finished, q, [this, runDir](int result) {
            if (result != QMessageBox::Yes)
            {
                return;
            }
            deleteWorkflowArchive(runDir);
        });
        messageBox->open();
    }

    void setupRunHistoryView()
    {
        if (!ui.listRunHistory || !ui.runHistoryFrame)
        {
            return;
        }

        // Replace QListView with QTreeView
        if (auto* layout = qobject_cast<QVBoxLayout*>(ui.runHistoryFrame->layout()))
        {
            layout->removeWidget(ui.listRunHistory);
            ui.listRunHistory->deleteLater();
            ui.listRunHistory = nullptr;

            historyView = new QTreeView(ui.runHistoryFrame);
            historyView->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
            historyView->setMinimumHeight(an::ui::tokens().spacing.xl * 4 + an::ui::tokens().spacing.xs);
            layout->addWidget(historyView, 1);
        }
        else
        {
            // Fallback if no layout (unlikely)
            return;
        }

        thumbnailCache = new RunHistoryThumbnailCache(q);
        historyModel = new WorkflowHistoryModel(q);
        historyDelegate = new WorkflowHistoryDelegate(q); // View handles painting via delegate
        historyDelegate->setThumbnailCache(thumbnailCache);
        historyDelegate->setView(historyView);

        historyView->setModel(historyModel);
        historyView->setItemDelegate(historyDelegate);
        historyView->setSelectionMode(QAbstractItemView::SingleSelection);
        historyView->setSelectionBehavior(QAbstractItemView::SelectRows);
        historyView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        historyView->setHeaderHidden(true);
        historyView->setIndentation(20);
        historyView->setUniformRowHeights(false); // Rows have different heights (header vs group)
        historyView->setRootIsDecorated(true);
        historyView->setAnimated(true);
        historyView->setExpandsOnDoubleClick(false); // Double click opens
        historyView->setAttribute(Qt::WA_Hover, true);
        if (historyView->viewport())
        {
            historyView->viewport()->setMouseTracking(true);
            historyView->viewport()->setAttribute(Qt::WA_Hover, true);
        }

        // Default expand the first item (most recent) whenever data changes?
        // Maybe do that in updateRunHistory or on reset.

        const QString runHistoryHelp =
            SummaryTab::tr("Run History\n\n"
                           "Last Execution: The most recent integration run for this output directory.\n"
                           "Archived: A previous run preserved in the project history.\n\n"
                           "Tip: Hover an entry for details; use the action buttons to open, view report, or re-run.");
        if (ui.lblRunHistoryTitle)
        {
            ui.lblRunHistoryTitle->setToolTip(runHistoryHelp);
        }
        historyView->setMouseTracking(true);

        QObject::connect(historyDelegate, &WorkflowHistoryDelegate::openAllRequested, [this](const QModelIndex& index) {
            // Open all masters in this workflow
            // iterate children
            int rowCount = historyModel->rowCount(index);
            for (int i = 0; i < rowCount; ++i)
            {
                QModelIndex child = historyModel->index(i, 0, index);
                QString path = child.data(WorkflowHistoryModel::OutputPathRole).toString();
                if (!path.isEmpty() && QFileInfo::exists(path))
                {
                    emit q->openRequested(path);
                }
            }
        });

        QObject::connect(historyDelegate, &WorkflowHistoryDelegate::openMasterRequested,
                         [this](const QModelIndex& index) {
                             const QString openTarget = index.data(WorkflowHistoryModel::OutputPathRole).toString();
                             if (!openTarget.isEmpty() && QFileInfo::exists(openTarget))
                             {
                                 emit q->openRequested(openTarget);
                             }
                         });

        QObject::connect(historyDelegate, &WorkflowHistoryDelegate::compareMasterRequested,
                         [this](const QModelIndex& index) { compareMastersForGroup(index); });

        QObject::connect(historyDelegate, &WorkflowHistoryDelegate::reportRequested, [this](const QModelIndex& index) {
            QString runDir = index.data(WorkflowHistoryModel::RunDirPathRole).toString();
            if (runDir.isEmpty())
            {
                const QString manifestPath = index.data(WorkflowHistoryModel::ManifestPathRole).toString();
                if (!manifestPath.isEmpty())
                {
                    runDir = QFileInfo(manifestPath).absolutePath();
                }
            }

            if (!runDir.isEmpty())
            {
                emit q->exportReportRequested(runDir);
                return;
            }

            const QString path = index.data(WorkflowHistoryModel::ReportPathRole).toString();
            if (!path.isEmpty())
            {
                an::ui::openLocalFile(path);
            }
        });

        QObject::connect(historyDelegate, &WorkflowHistoryDelegate::logRequested, [this](const QModelIndex& index) {
            QString path = index.data(WorkflowHistoryModel::LogPathRole).toString();
            if (!path.isEmpty())
            {
                // Open log file (using system handler or internal viewer?)
                // For now, emit openRequested (which usually opens images, but maybe text?)
                // Or we can add a log viewer signal.
                // Let's use openRequested for now, assuming it delegates to desktop services for non-images if needed,
                // or just open URL.
                // Actually ImageIntegrationProcess::onOpenRequested handles images.
                // Let's rely on OS open for log.
                an::ui::openLocalFile(path);
            }
        });

        QObject::connect(historyDelegate, &WorkflowHistoryDelegate::rerunRequested, [this](const QModelIndex& index) {
            QString runDir = index.data(WorkflowHistoryModel::RunDirPathRole).toString();
            if (runDir.isEmpty())
            {
                const QString manifestPath = index.data(WorkflowHistoryModel::ManifestPathRole).toString();
                if (!manifestPath.isEmpty())
                {
                    runDir = QFileInfo(manifestPath).absolutePath();
                }
            }
            if (!runDir.isEmpty())
            {
                emit q->rerunRequested(runDir);
            }
        });

        QObject::connect(historyDelegate, &WorkflowHistoryDelegate::deleteRequested,
                         [this](const QModelIndex& index) { confirmDeleteWorkflowArchive(index); });

        // Double click behavior
        QObject::connect(historyView, &QTreeView::doubleClicked, [this](const QModelIndex& index) {
            bool isGroup = index.data(WorkflowHistoryModel::IsGroupItemRole).toBool();
            if (isGroup)
            {
                const QString openTarget = index.data(WorkflowHistoryModel::OutputPathRole).toString();
                if (!openTarget.isEmpty() && QFileInfo::exists(openTarget))
                {
                    emit q->openRequested(openTarget);
                }
            }
            else
            {
                // Toggle expansion
                if (historyView->isExpanded(index))
                    historyView->collapse(index);
                else
                    historyView->expand(index);
            }
        });

        QObject::connect(thumbnailCache, &RunHistoryThumbnailCache::thumbnailReady, [this](const QString&) {
            if (historyView && historyView->viewport())
            {
                historyView->viewport()->update();
            }
        });

        QObject::connect(historyView, &QTreeView::expanded, [this](const QModelIndex& index) {
            if (!historyModel || !index.isValid())
            {
                return;
            }
            if (index.data(WorkflowHistoryModel::IsGroupItemRole).toBool())
            {
                return;
            }
            historyModel->setData(index, true, WorkflowHistoryModel::IsExpandedRole);
        });

        QObject::connect(historyView, &QTreeView::collapsed, [this](const QModelIndex& index) {
            if (!historyModel || !index.isValid())
            {
                return;
            }
            if (index.data(WorkflowHistoryModel::IsGroupItemRole).toBool())
            {
                return;
            }
            historyModel->setData(index, false, WorkflowHistoryModel::IsExpandedRole);
        });
    }

    void setupDebounceTimer()
    {
        debounceTimer = new QTimer(q);
        debounceTimer->setSingleShot(true);
        debounceTimer->setInterval(ImageIntegration::Config::kDebounceIntervalMs);
        QObject::connect(debounceTimer, &QTimer::timeout, [this]() { updateAll(); });
    }

    void setupNotesTimer()
    {
        notesTimer = new QTimer(q);
        notesTimer->setSingleShot(true);
        notesTimer->setInterval(350);
        QObject::connect(notesTimer, &QTimer::timeout, [this]() { saveSessionNotes(); });
    }

    void scheduleRefresh()
    {
        debounceTimer->start();
    }

    void setupConnections()
    {
        QObject::connect(model, &IntegrationDataModel::framesAdded, q, &SummaryTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::framesRemoved, q, &SummaryTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::groupingChanged, q, &SummaryTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::calibrationChanged, q, &SummaryTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::registrationChanged, q, &SummaryTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::integrationGroupsChanged, q, &SummaryTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::settingsChanged, q, &SummaryTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::outputDirectoryChanged, q, &SummaryTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::modelReset, q, &SummaryTab::refreshAll);

        if (ui.btnExecute)
        {
            QObject::connect(ui.btnExecute, &QPushButton::clicked, q, &SummaryTab::executeRequested);
        }

        if (ui.txtSessionNotes)
        {
            QObject::connect(ui.txtSessionNotes, &QPlainTextEdit::textChanged, [this]() {
                if (loadingNotes)
                {
                    return;
                }
                notesTimer->start();
                emit q->notesChanged(ui.txtSessionNotes->toPlainText());
            });
        }
    }

    void updateStatusCard()
    {
        WorkflowValidationResult result = model->validate();

        const size_t errorCount = std::count_if(result.issues.begin(), result.issues.end(), [](const auto& i) {
            return i.severity == ValidationIssue::Severity::Error;
        });
        const size_t warnCount = std::count_if(result.issues.begin(), result.issues.end(), [](const auto& i) {
            return i.severity == ValidationIssue::Severity::Warning;
        });

        const size_t lightCount = model->lightFrames().size();
        const size_t groupCount = model->integrationGroups().size();

        // P1 #1: Distinct Status Card Style
        // Give the status card a stronger border/presence to act as the "Head" of the tab.
        {
            QPalette cardPal = ui.statusCardFrame->palette();
            cardPal.setColor(QPalette::Window, q->palette().color(QPalette::Window));
            cardPal.setColor(QPalette::Mid, q->palette().color(QPalette::Mid));
            ui.statusCardFrame->setPalette(cardPal);
            ui.statusCardFrame->setProperty("anFrameRadius", 4);
            ui.statusCardFrame->setFrameShape(QFrame::StyledPanel);
        }

        if (errorCount > 0)
        {
            ui.lblStatusTitle->setText(SummaryTab::tr("Cannot Execute"));
            ui.lblStatusIcon->setPixmap(summaryPixmap(QStringLiteral("priority_high"), QSize(32, 32)));
            ui.btnExecute->setEnabled(false);
            ui.btnExecute->setText(SummaryTab::tr("Fix Issues First"));
            ui.btnExecute->setToolTip(SummaryTab::tr(
                "Cannot start integration until all errors are resolved. Check the issues listed above."));
        }
        else if (warnCount > 0)
        {
            ui.lblStatusTitle->setText(SummaryTab::tr("Ready with Warnings"));
            ui.lblStatusIcon->setPixmap(summaryPixmap(QStringLiteral("warning"), QSize(32, 32)));
            ui.btnExecute->setEnabled(true);
            ui.btnExecute->setText(SummaryTab::tr("Execute Integration"));
            ui.btnExecute->setToolTip(SummaryTab::tr(
                "Start the integration workflow. Note: There are warnings that may affect the results."));
        }
        else
        {
            ui.lblStatusTitle->setText(SummaryTab::tr("Ready to Execute"));
            ui.lblStatusIcon->setPixmap(summaryPixmap(QStringLiteral("check"), QSize(32, 32)));
            ui.btnExecute->setEnabled(true);
            ui.btnExecute->setText(SummaryTab::tr("Execute Integration"));
            ui.btnExecute->setToolTip(
                SummaryTab::tr("Start the integration workflow. This will calibrate, register, and stack your light "
                               "frames according to the configured settings."));
        }

        QStringList summaryLines;
        if (errorCount > 0)
        {
            summaryLines << SummaryTab::tr("%1 issue%2 to resolve")
                                .arg(errorCount)
                                .arg(errorCount == 1 ? QString() : QStringLiteral("s"));
        }
        else if (warnCount > 0)
        {
            summaryLines << SummaryTab::tr("%1 warning%2 to review")
                                .arg(warnCount)
                                .arg(warnCount == 1 ? QString() : QStringLiteral("s"));
        }

        if (lightCount == 0)
        {
            summaryLines << SummaryTab::tr("Add light frames to get started.");
        }
        else
        {
            summaryLines << SummaryTab::tr("%1 light frames → %2 stacked image(s)").arg(lightCount).arg(groupCount);
        }
        ui.lblStatusSummary->setText(summaryLines.join(QStringLiteral("\n")));

        if (!ui.statusIssuesLayout || !ui.statusIssuesWidget)
        {
            return;
        }

        while (QLayoutItem* item = ui.statusIssuesLayout->takeAt(0))
        {
            delete item->widget();
            delete item;
        }

        if (result.issues.empty())
        {
            ui.statusIssuesWidget->setVisible(false);
            return;
        }

        std::vector<ValidationIssue> sortedIssues = result.issues;
        std::stable_sort(sortedIssues.begin(), sortedIssues.end(),
                         [](const ValidationIssue& a, const ValidationIssue& b) {
                             return static_cast<int>(a.severity) < static_cast<int>(b.severity);
                         });

        ui.statusIssuesWidget->setVisible(true);

        // P0 #2: Theme-aware colors
        for (const auto& issue : sortedIssues)
        {
            const QString groupKeyForSelection = issue.filePath;
            auto emitFixForIssue = [this, target = issue.fixTarget, groupKeyForSelection]() {
                if (!groupKeyForSelection.isEmpty())
                {
                    emit q->fixWithGroupRequested(target, groupKeyForSelection);
                }
                else
                {
                    emit q->fixRequested(target);
                }
            };

            QFrame* cardWidget = nullptr;
            if (issue.fixTarget != FixTarget::None)
            {
                cardWidget = new ClickableIssueCard(emitFixForIssue, ui.statusIssuesWidget);
            }
            else
            {
                cardWidget = new QFrame(ui.statusIssuesWidget);
            }
            cardWidget->setObjectName(QStringLiteral("statusIssueCard"));
            cardWidget->setFrameShape(QFrame::StyledPanel);
            cardWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

            const bool isError = (issue.severity == ValidationIssue::Severity::Error);
            const auto bgRole =
                isError ? an::ui::SemanticColorRole::DangerBackground : an::ui::SemanticColorRole::WarningBackground;
            const auto borderRole =
                isError ? an::ui::SemanticColorRole::DangerBorder : an::ui::SemanticColorRole::WarningBorder;
            const QColor bgColor = an::ui::semanticColor(bgRole);
            const QColor borderColor = an::ui::semanticColor(borderRole);

            {
                QPalette cardPal = cardWidget->palette();
                cardPal.setColor(QPalette::Window, bgColor);
                cardPal.setColor(QPalette::Mid, borderColor);
                cardWidget->setPalette(cardPal);
                cardWidget->setProperty("anFrameRadius", an::ui::tokens().radius.sm);
            }

            const auto& spacing = an::ui::tokens().spacing;
            auto* cardLayout = new QHBoxLayout(cardWidget);
            cardLayout->setContentsMargins(spacing.sm, spacing.xs, spacing.sm, spacing.xs);
            cardLayout->setSpacing(spacing.sm);

            auto* iconLabel = new QLabel(cardWidget);
            iconLabel->setPixmap(summaryPixmap(isError ? QStringLiteral("priority_high") : QStringLiteral("warning"),
                                               QSize(spacing.lg, spacing.lg)));
            iconLabel->setMinimumSize(spacing.lg, spacing.lg);
            iconLabel->setAlignment(Qt::AlignCenter);
            cardLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

            auto* textContainer = new QWidget(cardWidget);
            auto* textVLayout = new QVBoxLayout(textContainer);
            textVLayout->setContentsMargins(QMargins());
            textVLayout->setSpacing(spacing.xs);

            QString groupName;
            if (!issue.filePath.isEmpty())
            {
                groupName = issue.filePath;
                if (const auto parsed = GroupKey::fromString(groupName); parsed.has_value())
                {
                    groupName = parsed->toCompactString();
                }
                groupName.replace(QLatin1Char('|'), QStringLiteral(" · "));
            }

            const bool isWarning = (issue.severity == ValidationIssue::Severity::Warning);
            const QString displayText = (isWarning && !groupName.isEmpty())
                                            ? SummaryTab::tr("%1 — %2").arg(issue.message, groupName)
                                            : issue.message;
            auto* messageLabel = new QLabel(displayText, textContainer);
            messageLabel->setWordWrap(false);
            textVLayout->addWidget(messageLabel);

            cardLayout->addWidget(textContainer, 1);

            if (isWarning || !issue.filePath.isEmpty() || issue.code != ValidationErrorCode::Unknown)
            {
                QStringList tooltipParts;
                tooltipParts << QStringLiteral("<b>Details:</b> %1").arg(issue.message.toHtmlEscaped());
                const QString category = issue.category();
                if (!category.isEmpty())
                {
                    tooltipParts << QStringLiteral("<b>Category:</b> %1").arg(category);
                }
                if (!groupName.isEmpty())
                {
                    tooltipParts << QStringLiteral("<b>Source group:</b> %1").arg(groupName.toHtmlEscaped());
                }
                const QString severityText =
                    isError ? SummaryTab::tr("Error (blocks execution)") : SummaryTab::tr("Warning (can proceed)");
                tooltipParts << QStringLiteral("<b>Severity:</b> %1").arg(severityText);

                if (issue.fixTarget != FixTarget::None)
                {
                    QString suggestion;
                    switch (issue.fixTarget)
                    {
                    case FixTarget::BiasFrames:
                        suggestion = SummaryTab::tr("Add bias frames matching this group's camera settings");
                        break;
                    case FixTarget::DarkFrames:
                        suggestion = SummaryTab::tr("Add dark frames matching this group's exposure and temperature");
                        break;
                    case FixTarget::FlatFrames:
                        suggestion = SummaryTab::tr("Add flat frames matching this group's filter and binning");
                        break;
                    case FixTarget::CalibrationTab:
                        suggestion = SummaryTab::tr("Review calibration settings and frame matching");
                        break;
                    case FixTarget::OutputDirectory:
                        suggestion = SummaryTab::tr("Set a valid output directory");
                        break;
                    default:
                        break;
                    }
                    if (!suggestion.isEmpty())
                    {
                        tooltipParts << QStringLiteral("<b>Suggestion:</b> %1").arg(suggestion);
                    }
                }

                const QString tooltip = tooltipParts.join(QStringLiteral("<br/>"));
                cardWidget->setToolTip(tooltip);
                messageLabel->setToolTip(tooltip);
                iconLabel->setToolTip(tooltip);
            }

            if (issue.fixTarget != FixTarget::None)
            {
                auto* fixButton = new QToolButton(cardWidget);
                fixButton->setText(SummaryTab::tr("Go to"));
                fixButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                fixButton->setCursor(Qt::PointingHandCursor);

                fixButton->setIcon(summaryIcon(QStringLiteral("next")));

                fixButton->setAutoRaise(true);
                fixButton->setFont(an::ui::tokens().typography.caption);

                QObject::connect(fixButton, &QToolButton::clicked, emitFixForIssue);
                cardLayout->addWidget(fixButton, 0, Qt::AlignVCenter);
            }

            ui.statusIssuesLayout->addWidget(cardWidget);
        }
    }

    void updateIntegrationSummary()
    {
        if (!ui.integrationReportLayout)
        {
            return;
        }

        // Clear existing items
        QLayoutItem* item;
        while ((item = ui.integrationReportLayout->takeAt(0)) != nullptr)
        {
            if (item->widget())
            {
                delete item->widget();
            }
            delete item;
        }

        int currentRow = 0;
        auto addRow = [&](const QString& key, const QString& value, const QString& tooltip = QString()) {
            auto* lblKey = new QLabel(key, ui.integrationReportWidget);
            // P0 #3: Use palette-relative colors. PlaceholderText is usually a good choice for labels/keys.
            {
                QPalette p = lblKey->palette();
                p.setColor(QPalette::WindowText, q->palette().color(QPalette::PlaceholderText));
                lblKey->setPalette(p);
                lblKey->setFont(an::ui::tokens().typography.heading3);
            }
            lblKey->setAlignment(Qt::AlignTop | Qt::AlignLeft);

            auto* lblValue = new QLabel(value, ui.integrationReportWidget);
            lblValue->setWordWrap(true);
            lblValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
            lblValue->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            if (!tooltip.isEmpty())
            {
                lblValue->setToolTip(tooltip);
            }

            ui.integrationReportLayout->addWidget(lblKey, currentRow, 0);
            ui.integrationReportLayout->addWidget(lblValue, currentRow, 1);
            currentRow++;
        };

        const auto frames = model->lightFrames().frames();
        if (frames.empty())
        {
            addRow(SummaryTab::tr("Total Exposure"), QStringLiteral("-"));
            addRow(SummaryTab::tr("By Filter"), QStringLiteral("-"));
            addRow(SummaryTab::tr("Capture Dates"), QStringLiteral("-"));
            addRow(SummaryTab::tr("Targets"), QStringLiteral("-"));
            addRow(SummaryTab::tr("Source Directories"), QStringLiteral("-"));
            addRow(SummaryTab::tr("Camera / Setup"), QStringLiteral("-"));
            return;
        }

        double totalSeconds = 0.0;
        QMap<QString, BreakdownStats> filterStats;
        QMap<QString, BreakdownStats> objectStats;
        QMap<QString, QString> objectDisplayName;
        QMap<QString, BreakdownStats> cameraStats;
        QMap<QString, QString> cameraDisplayName;
        QStringList sourceDirs;
        QDateTime minDate;
        QDateTime maxDate;

        for (const auto& frame : frames)
        {
            if (frame.excluded)
            {
                continue;
            }

            const double exposure = frame.metadata.exposureSeconds();
            totalSeconds += exposure;

            QString filter = frame.metadata.filter().trimmed();
            if (filter.isEmpty())
            {
                filter = QStringLiteral("Unfiltered");
            }
            {
                BreakdownStats& stats = filterStats[filter];
                stats.seconds += exposure;
                stats.frames += 1;
            }

            {
                const QString objectName = frame.effectiveObjectName().trimmed();
                const QString display = objectName.isEmpty() ? QStringLiteral("Unknown") : objectName;
                const QString key = normalizeKey(display);
                objectDisplayName.insert(key, display);
                BreakdownStats& stats = objectStats[key];
                stats.seconds += exposure;
                stats.frames += 1;
            }

            {
                const QString cameraLabel = cameraSetupLabel(frame).trimmed();
                const QString display = cameraLabel.isEmpty() ? QStringLiteral("Unknown") : cameraLabel;
                const QString key = normalizeKey(display);
                cameraDisplayName.insert(key, display);
                BreakdownStats& stats = cameraStats[key];
                stats.seconds += exposure;
                stats.frames += 1;
            }

            const QFileInfo info(frame.displayPath);
            if (!info.absolutePath().isEmpty())
            {
                sourceDirs << info.absolutePath();
            }

            const QDateTime captured = frame.metadata.capturedAtUtc();
            if (captured.isValid())
            {
                if (!minDate.isValid() || captured < minDate)
                {
                    minDate = captured;
                }
                if (!maxDate.isValid() || captured > maxDate)
                {
                    maxDate = captured;
                }
            }
        }

        const QString totalExposure = formatDuration(totalSeconds);

        const BreakdownText filterText = formatFilterBreakdown(filterStats);

        QString captureRange = QStringLiteral("-");
        if (minDate.isValid() && maxDate.isValid())
        {
            const QString start = minDate.toLocalTime().date().toString(QStringLiteral("yyyy-MM-dd"));
            const QString end = maxDate.toLocalTime().date().toString(QStringLiteral("yyyy-MM-dd"));
            captureRange = start == end ? start : SummaryTab::tr("%1 → %2").arg(start, end);
        }

        QString targets = QStringLiteral("-");
        if (!objectStats.isEmpty())
        {
            const BreakdownText objectText =
                formatBreakdownList(objectStats, objectDisplayName, SummaryTab::tr("targets"));
            targets = objectText.text;
        }

        QString cameraSetup = QStringLiteral("-");
        if (!cameraStats.isEmpty())
        {
            const BreakdownText cameraText =
                formatBreakdownList(cameraStats, cameraDisplayName, SummaryTab::tr("setups"));
            cameraSetup = cameraText.text;
        }

        const QString srcDirs = summarizeDirectories(sourceDirs);

        addRow(SummaryTab::tr("Total Exposure"), totalExposure);
        addRow(SummaryTab::tr("By Filter"), filterText.text, filterText.tooltip);
        addRow(SummaryTab::tr("Capture Dates"), captureRange);
        addRow(SummaryTab::tr("Targets"), targets);
        addRow(SummaryTab::tr("Source Directories"), srcDirs);
        addRow(SummaryTab::tr("Camera / Setup"), cameraSetup);
    }

    QString notesFilePath(const QString& outputDir) const
    {
        if (outputDir.trimmed().isEmpty())
        {
            return QString();
        }
        const QDir root(outputDir);
        const QString notesDir = root.filePath(QStringLiteral(".afternight/image_integration"));
        return QDir(notesDir).filePath(QStringLiteral("session_notes.txt"));
    }

    void loadSessionNotes()
    {
        const QString outputDir = model->outputDirectory().trimmed();
        if (outputDir == lastNotesDir)
        {
            return;
        }
        lastNotesDir = outputDir;

        if (!ui.txtSessionNotes)
        {
            return;
        }

        loadingNotes = true;
        ui.txtSessionNotes->clear();

        const QString path = notesFilePath(outputDir);
        if (!path.isEmpty())
        {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly))
            {
                ui.txtSessionNotes->setPlainText(QString::fromUtf8(file.readAll()));
            }
        }

        loadingNotes = false;
    }

    void saveSessionNotes()
    {
        if (!ui.txtSessionNotes)
        {
            return;
        }

        const QString outputDir = model->outputDirectory().trimmed();
        const QString path = notesFilePath(outputDir);
        if (path.isEmpty())
        {
            return;
        }

        const QFileInfo pathInfo(path);
        QDir().mkpath(pathInfo.absolutePath());

        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            file.write(ui.txtSessionNotes->toPlainText().toUtf8());
        }
    }

    void updateSessionNotesState()
    {
        if (!ui.txtSessionNotes || !ui.lblNotesHint)
        {
            return;
        }

        const QString outputDir = model->outputDirectory().trimmed();
        const bool enabled = !outputDir.isEmpty();

        // P3 #7: Collapse logic
        ui.txtSessionNotes->setVisible(enabled);
        ui.lblNotesHint->setVisible(!enabled);
        ui.txtSessionNotes->setEnabled(enabled);

        if (enabled)
        {
            loadSessionNotes();
        }
        else
        {
            lastNotesDir.clear();
            loadingNotes = true;
            ui.txtSessionNotes->clear();
            loadingNotes = false;
        }
    }

    void updateRunHistory()
    {
        if (!historyModel)
        {
            return;
        }

        const QString outputDir = model->outputDirectory();

        // Ensure the history directory is pointing to the correct 'history' subdirectory
        QString historyDir;
        if (!outputDir.isEmpty())
        {
            historyDir = QDir(outputDir).filePath("history");
        }

        historyModel->setHistoryDirectory(historyDir);
        // Runs usually reuse the same output/history path, so force reload to pick up new entries.
        historyModel->refresh();

        if (historyView)
        {
            const int rows = historyModel->rowCount();
            for (int row = 0; row < rows; ++row)
            {
                const QModelIndex workflowIndex = historyModel->index(row, 0);
                if (!workflowIndex.isValid())
                {
                    continue;
                }

                const bool expanded = workflowIndex.data(WorkflowHistoryModel::IsExpandedRole).toBool();
                if (expanded)
                {
                    historyView->expand(workflowIndex);
                }
                else
                {
                    historyView->collapse(workflowIndex);
                }
            }
        }
    }

    void updateResourceStatus()
    {
        if (!ui.lblResourceStatus || !ui.lblResourceStatusIcon || !ui.resourceStatusWidget)
        {
            return;
        }

        const QString outputDir = model->outputDirectory().trimmed();
        if (outputDir.isEmpty())
        {
            ui.lblResourceStatus->setText(SummaryTab::tr("Disk: --"));
            ui.lblResourceStatus->setToolTip(QString());
            ui.lblResourceStatusIcon->setPixmap(summaryPixmap(QStringLiteral("info"), QSize(16, 16)));
            return;
        }

        const QStorageInfo storage(outputDir);
        const qint64 freeBytes = storage.isValid() ? static_cast<qint64>(storage.bytesAvailable()) : 0;

        qint64 avgLightFileBytes = 0;
        int includedLights = 0;
        const auto frames = model->lightFrames().frames();
        qint64 totalSize = 0;
        int counted = 0;
        for (const auto& frame : frames)
        {
            if (frame.excluded)
            {
                continue;
            }
            ++includedLights;
            const QFileInfo info(frame.displayPath);
            if (info.exists())
            {
                totalSize += info.size();
                ++counted;
            }
        }

        if (counted > 0 && totalSize > 0)
        {
            avgLightFileBytes = totalSize / counted;
        }

        const auto& groupKeys = model->integrationGroups();
        const int groupCount = (std::max<int>)(1, static_cast<int>(groupKeys.size()));

        const auto& stackingSettings = model->stackingSettings(FrameType::Light);
        bool anyDrizzle = false;
        bool anyNonDrizzle = false;
        int rejectionMapFiles = 0;
        for (const auto& key : groupKeys)
        {
            auto it = stackingSettings.find(key);
            if (it == stackingSettings.end())
            {
                continue;
            }

            const auto& settings = it->second;
            anyDrizzle = anyDrizzle || settings.drizzle.enabled;
            anyNonDrizzle = anyNonDrizzle || !settings.drizzle.enabled;

            if (settings.createRejectionMaps && isAverageStackMethod(settings.stackMethod))
            {
                rejectionMapFiles += settings.mergeRejectionMaps ? 1 : 2;
            }
        }

        const qint64 stackedBytes = (avgLightFileBytes > 0) ? (avgLightFileBytes * groupCount) : 0;
        const qint64 rejectionBytes = (avgLightFileBytes > 0) ? (avgLightFileBytes * rejectionMapFiles) : 0;

        // Calculate calibrated frame count excluding pre-calibrated groups
        const bool calibrationToDisk = (model->lightCalibrationCacheMode() == LightCalibrationCacheMode::Disk);
        int calibratedLightCount = 0;
        if (calibrationToDisk)
        {
            const auto& lightCalibSettings = model->lightCalibrationSettings();
            for (const auto& group : model->lightGroups())
            {
                // Check if this group is pre-calibrated
                auto calIt = lightCalibSettings.find(group.key);
                const bool isPreCalibrated = (calIt != lightCalibSettings.end() && calIt->second.preCalibrated);

                if (!isPreCalibrated)
                {
                    for (const auto& frame : group.frames)
                    {
                        if (!frame.excluded)
                        {
                            ++calibratedLightCount;
                        }
                    }
                }
            }
        }
        const qint64 calibratedBytes = (calibrationToDisk && avgLightFileBytes > 0 && calibratedLightCount > 0)
                                           ? (avgLightFileBytes * static_cast<qint64>(calibratedLightCount))
                                           : 0;

        const qint64 registeredBytes = (anyNonDrizzle && avgLightFileBytes > 0 && includedLights > 0)
                                           ? (avgLightFileBytes * static_cast<qint64>(includedLights))
                                           : 0;

        qint64 drizzlePixmapBytes = 0;
        if (anyDrizzle)
        {
            for (const auto& frame : frames)
            {
                if (frame.excluded)
                {
                    continue;
                }

                const QSize dims = frame.metadata.dimensions();
                if (dims.width() <= 0 || dims.height() <= 0)
                {
                    continue;
                }

                const qint64 w = static_cast<qint64>(dims.width());
                const qint64 h = static_cast<qint64>(dims.height());
                // Pixmaps store x/y maps as 2 floats per pixel.
                drizzlePixmapBytes += w * h * 8;
            }
        }

        const qint64 estimatedBytes =
            stackedBytes + rejectionBytes + calibratedBytes + registeredBytes + drizzlePixmapBytes;

        const bool tight = (estimatedBytes > 0 && freeBytes > 0 && freeBytes < estimatedBytes);

        const QString freeLabel = formatOutputBytes(freeBytes);
        const QString needLabel = formatOutputBytes(estimatedBytes);
        ui.lblResourceStatus->setText(estimatedBytes > 0
                                          ? SummaryTab::tr("Disk space: %1 free • Need ~%2").arg(freeLabel, needLabel)
                                          : SummaryTab::tr("Disk space: %1 free").arg(freeLabel));

        QString tooltip;
        tooltip += SummaryTab::tr("Available: %1\n").arg(freeLabel);
        if (estimatedBytes > 0)
        {
            tooltip += SummaryTab::tr("Estimated needed: ~%1\n\n").arg(needLabel);
            if (stackedBytes > 0)
            {
                tooltip += SummaryTab::tr("- Stacked outputs (%1): ~%2\n")
                               .arg(groupCount)
                               .arg(formatOutputBytes(stackedBytes));
            }
            if (rejectionBytes > 0)
            {
                tooltip += SummaryTab::tr("- Rejection maps (%1): ~%2\n")
                               .arg(rejectionMapFiles)
                               .arg(formatOutputBytes(rejectionBytes));
            }
            if (calibratedBytes > 0)
            {
                tooltip += SummaryTab::tr("- Calibrated lights (%1): ~%2\n  %3\n")
                               .arg(includedLights)
                               .arg(formatOutputBytes(calibratedBytes))
                               .arg(QDir(outputDir).filePath(QStringLiteral("calibrated_lights")));
            }
            if (registeredBytes > 0)
            {
                tooltip += SummaryTab::tr("- Registered lights (%1): ~%2\n  %3\n")
                               .arg(includedLights)
                               .arg(formatOutputBytes(registeredBytes))
                               .arg(QDir(outputDir).filePath(QStringLiteral("registered_lights")));
            }
            if (drizzlePixmapBytes > 0)
            {
                tooltip += SummaryTab::tr("- Drizzle pixmaps (%1): ~%2\n  %3\n")
                               .arg(includedLights)
                               .arg(formatOutputBytes(drizzlePixmapBytes))
                               .arg(QDir(outputDir).filePath(
                                   QStringLiteral(".afternight/image_integration/artifacts/drizzle/[group]/pixmaps")));
            }

            tooltip += QStringLiteral("\n");
            tooltip += SummaryTab::tr("Notes:\n");
            tooltip += SummaryTab::tr("- Sizes are estimates based on selected light frames.\n");
            if (drizzlePixmapBytes > 0)
            {
                tooltip +=
                    SummaryTab::tr("- Drizzle pixmaps are estimated from frame dimensions (2 floats per pixel).\n");
            }
        }
        ui.lblResourceStatus->setToolTip(tooltip.trimmed());

        ui.lblResourceStatusIcon->setPixmap(
            summaryPixmap(tight ? QStringLiteral("warning") : QStringLiteral("check"), QSize(16, 16)));

        if (tight)
        {
            const QColor bg = an::ui::semanticColor(an::ui::SemanticColorRole::WarningBackground);
            const QColor border = an::ui::semanticColor(an::ui::SemanticColorRole::WarningBorder);

            QPalette resPal = ui.resourceStatusWidget->palette();
            resPal.setColor(QPalette::Window, bg);
            resPal.setColor(QPalette::Mid, border);
            ui.resourceStatusWidget->setPalette(resPal);
            ui.resourceStatusWidget->setProperty("anFrameRadius", an::ui::tokens().radius.sm);
            ui.resourceStatusWidget->setFrameShape(QFrame::StyledPanel);
        }
        else
        {
            ui.resourceStatusWidget->setProperty("anFrameRadius", QVariant());
        }
    }

    void updateAll()
    {
        updateStatusCard();
        updateIntegrationSummary();
        updateSessionNotesState();
        updateRunHistory();
        updateResourceStatus();
        emit q->refreshed();
    }
};

SummaryTab::SummaryTab(IntegrationDataModel* model, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model))
{
}

SummaryTab::~SummaryTab()
{
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);

    if (m_impl && m_impl->debounceTimer)
    {
        m_impl->debounceTimer->stop();
    }
    if (m_impl && m_impl->notesTimer)
    {
        m_impl->notesTimer->stop();
    }
}

void SummaryTab::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (!event || event->type() != QEvent::PaletteChange)
    {
        return;
    }

    if (m_impl)
    {
        m_impl->applyBackground();
        m_impl->styleExecuteButton();
        m_impl->updateAll();
    }
}

void SummaryTab::setLastSuccessfulRun(const QDateTime& whenUtc, int lightFrameCount, const QString& outputDir)
{
    Q_UNUSED(whenUtc)
    Q_UNUSED(lightFrameCount)
    Q_UNUSED(outputDir)

    if (m_impl)
    {
        m_impl->updateRunHistory();
    }
}

void SummaryTab::refreshAll()
{
    if (!m_impl)
    {
        return;
    }
    m_impl->scheduleRefresh();
}
