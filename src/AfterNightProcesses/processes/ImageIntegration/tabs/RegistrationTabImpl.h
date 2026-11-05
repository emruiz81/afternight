// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// Private implementation header for RegistrationTab, shared by RegistrationTab.cpp,
// RegistrationTabHelpers.cpp, RegistrationTabTables.cpp and RegistrationTabActions.cpp.
//
// Deliberately NOT self-contained: include it only after the RegistrationTab
// translation units' common include block - it relies on the model, workflow,
// dialog and Qt headers those units already pull in.

// The tab used to carry its own anonymous-namespace copy of integrationIcon();
// it reuses the shared ImageIntegration::detail one instead.
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"

namespace ImageIntegration::detail
{

struct RegistrationGroupEntry
{
    RegistrationGroupKey key;
    std::vector<FrameId> frames;
};

struct MetricRow
{
    FrameQualityMetrics metrics;
    QString outputPath;
};

struct QualityThresholdConfig
{
    bool minScoreEnabled = false;
    double minScore = 0.0;
    bool minStarsEnabled = false;
    int minStars = 0;
    bool minSnrEnabled = false;
    double minSnr = 0.0;
    bool maxFwhmEnabled = false;
    double maxFwhm = 0.0;
    bool maxEccEnabled = false;
    double maxEcc = 0.0;

    [[nodiscard]] bool anyEnabled() const noexcept
    {
        return minScoreEnabled || minStarsEnabled || minSnrEnabled || maxFwhmEnabled || maxEccEnabled;
    }
};

struct FrameScoreContext
{
    bool hasAnyNamedFilter = false;
    QHash<QString, FilterStatistics> filterStats;
};

struct AutoReferenceChoice
{
    enum class Mode
    {
        SingleFilter,
        AllFilters
    };

    Mode mode = Mode::SingleFilter;
    QString filterName;
};

struct AutoReferenceContext
{
    std::unordered_map<FrameId, QString> filterById; // only for frames with known filter
    std::optional<AutoReferenceChoice> choice;
};

struct SuggestedThresholds
{
    double minScore = 0.0;
    int minStars = 0;
    double minSnr = 0.0;
    double maxFwhm = 0.0;
    double maxEcc = 0.0;
};

enum class QualityLevel
{
    Excellent,
    Good,
    Fair,
    Poor
};

struct GroupStatistics
{
    double starsQ1 = 0.0, starsQ3 = 0.0;
    double fwhmQ1 = 0.0, fwhmQ3 = 0.0;
    double eccQ1 = 0.0, eccQ3 = 0.0;
    double snrQ1 = 0.0, snrQ3 = 0.0;
};

constexpr int kStatusCol = 0;
constexpr int kFrameCol = 1;
constexpr int kFilterCol = 2;
constexpr int kStarsCol = 3;
constexpr int kFwhmCol = 4;
constexpr int kSnrCol = 5;
constexpr int kEccCol = 6;
constexpr int kScoreCol = 7;
constexpr int kQualityCol = 8;

constexpr int kGroupTableGroupCol = 0;
constexpr int kGroupTableReferenceCol = 6;
constexpr int kGroupTableStatusCol = 7;

const QString kReferenceIcon = QStringLiteral("★");
const QString kExcludedIcon = QStringLiteral("✗");
const QString kOkIcon = QStringLiteral("✓");
const QString kWarnIcon = QStringLiteral("⚠");
const QString kFailIcon = QStringLiteral("❌");

// Defined in RegistrationTabHelpers.cpp.
[[nodiscard]] bool frameFailsQualityThresholds(const FrameQualityMetrics& metrics, const double score,
                                               const QualityThresholdConfig& thresholds);
QString translatedRegistrationWorkReason(const RegistrationWorkReason& reason);
QString registrationTransformDisplayName(const RegistrationSettings& settings);
void applyStatusTone(QLabel* label, const char* tone);
QString buildFilterNormalizedMetricTooltip(const QString& metricName, double absoluteValue, const QString& filterKey,
                                           const QHash<QString, FilterStatistics>& filterStats, bool higherIsBetter);
[[nodiscard]] std::optional<FrameId>
selectBestReferenceFrameWithChoice(QWidget* parent, const std::unordered_map<QString, MetricRow>& metricsMap,
                                   const std::vector<FrameId>& frameIds, const AutoReferenceContext& ctx);
[[nodiscard]] QString buildAutoReferenceResultText(const FrameId& bestId,
                                                   const std::unordered_map<QString, MetricRow>& metricsMap,
                                                   const std::vector<FrameId>& frameIds,
                                                   const AutoReferenceContext& ctx);
/// Asks which filter Auto Reference compares frames by, without a nested event loop: `chosen` receives the choice once
/// the person answers, or nothing when they cancel. A group of one filter is not asked about, and `chosen` receives
/// nothing at once.
void promptAutoReferenceChoice(QWidget* parent, const QStringList& detectedFilters,
                               std::function<void(std::optional<AutoReferenceChoice>)> chosen);
double quantileSorted(const std::vector<double>& sorted, const double q);
SuggestedThresholds suggestThresholdsFromIqr(std::vector<double> scores, std::vector<double> stars,
                                             std::vector<double> snr, std::vector<double> fwhm,
                                             std::vector<double> ecc);
GroupStatistics computeGroupStatistics(const std::vector<FrameQualityMetrics>& metrics);
QColor resolvedTableCellTextColor(const QTableWidget* table, const QColor& backgroundColor,
                                  const QColor& preferredTextColor = QColor());
QColor getMetricColor(double value, double q1, double q3, bool higherIsBetter, bool isDarkTheme);
QualityLevel computeFrameQuality(const FrameQualityMetrics& metrics, const GroupStatistics& stats);
QString qualityIcon(QualityLevel level);
QString qualityTooltip(QualityLevel level);

} // namespace ImageIntegration::detail

// This header is private to the RegistrationTab units; every one of them wants
// these names unqualified, exactly as they read before the split.
using ImageIntegration::detail::applyStatusTone;
using ImageIntegration::detail::AutoReferenceChoice;
using ImageIntegration::detail::AutoReferenceContext;
using ImageIntegration::detail::buildAutoReferenceResultText;
using ImageIntegration::detail::buildFilterNormalizedMetricTooltip;
using ImageIntegration::detail::computeFrameQuality;
using ImageIntegration::detail::computeGroupStatistics;
using ImageIntegration::detail::frameFailsQualityThresholds;
using ImageIntegration::detail::FrameScoreContext;
using ImageIntegration::detail::getMetricColor;
using ImageIntegration::detail::GroupStatistics;
using ImageIntegration::detail::integrationIcon;
using ImageIntegration::detail::kEccCol;
using ImageIntegration::detail::kExcludedIcon;
using ImageIntegration::detail::kFailIcon;
using ImageIntegration::detail::kFilterCol;
using ImageIntegration::detail::kFrameCol;
using ImageIntegration::detail::kFwhmCol;
using ImageIntegration::detail::kGroupTableGroupCol;
using ImageIntegration::detail::kGroupTableReferenceCol;
using ImageIntegration::detail::kGroupTableStatusCol;
using ImageIntegration::detail::kOkIcon;
using ImageIntegration::detail::kQualityCol;
using ImageIntegration::detail::kReferenceIcon;
using ImageIntegration::detail::kScoreCol;
using ImageIntegration::detail::kSnrCol;
using ImageIntegration::detail::kStarsCol;
using ImageIntegration::detail::kStatusCol;
using ImageIntegration::detail::kWarnIcon;
using ImageIntegration::detail::MetricRow;
using ImageIntegration::detail::openNonBlockingMessageBox;
using ImageIntegration::detail::promptAutoReferenceChoice;
using ImageIntegration::detail::qualityIcon;
using ImageIntegration::detail::QualityLevel;
using ImageIntegration::detail::QualityThresholdConfig;
using ImageIntegration::detail::qualityTooltip;
using ImageIntegration::detail::quantileSorted;
using ImageIntegration::detail::RegistrationGroupEntry;
using ImageIntegration::detail::registrationTransformDisplayName;
using ImageIntegration::detail::resolvedTableCellTextColor;
using ImageIntegration::detail::selectBestReferenceFrameWithChoice;
using ImageIntegration::detail::SuggestedThresholds;
using ImageIntegration::detail::suggestThresholdsFromIqr;
using ImageIntegration::detail::translatedRegistrationWorkReason;

class RegistrationTab::Impl
{
public:
    RegistrationTab* q;
    an::ui::imageintegration::RegistrationTabUiElements ui;
    IntegrationDataModel* model;
    QUndoStack* undoStack = nullptr;
    QTimer* debounceTimer = nullptr;
    std::vector<RegistrationGroupEntry> cachedGroups;
    std::vector<FrameId> displayedFrames;
    QualityDistributionWidget* chartWidget = nullptr;
    QPushButton* btnRunRegistrationGroup = nullptr;
    QPushButton* btnClearRegistrationGroup = nullptr;

    // Cached excluded frame count for the currently selected group (for button state)
    int cachedExcludedCount = 0;

    // Cached database connection for metrics
    RegistrationMetricsDataAccess metricsDataAccess;
    QString lastDbError;

    std::optional<WorkflowManifest> registrationAnalysisManifest;
    QString registrationAnalysisManifestError;
    bool registrationAnalysisManifestPrepared = false;

    bool ignoreSelectionChange = false;
    bool pendingMetricsDbReset = false;
    bool applyingThemeStyling = false;

    [[nodiscard]] static QMargins cardContentMargins()
    {
        const auto& spacing = an::ui::tokens().spacing;
        return QMargins(spacing.sm, spacing.md, spacing.sm, spacing.sm);
    }

    ~Impl()
    {
        closeMetricsDb();
    }

    void closeMetricsDb()
    {
        metricsDataAccess.closeMetricsDb();
    }

    [[nodiscard]] RegistrationSettings settingsForGroup(const GroupKey& machine_key) const
    {
        if (const auto it = model->registrationSettings().find(machine_key); it != model->registrationSettings().end())
        {
            return it->second;
        }
        return {};
    }

    void scheduleRefresh()
    {
        debounceTimer->start();
    }

    void invalidateRegistrationAnalysisManifest()
    {
        registrationAnalysisManifest.reset();
        registrationAnalysisManifestError.clear();
        registrationAnalysisManifestPrepared = false;
    }

    void excludeSelectedFrames()
    {
        const auto selected = selectedFramePathsByExclusion();
        setFramesExcluded(selected.included, true);
    }

    void includeSelectedFrames()
    {
        const auto selected = selectedFramePathsByExclusion();
        setFramesExcluded(selected.excluded, false);
    }

    void updateAll()
    {
        updateGroupTable();
        updateFrameTable();
        updateActionButtons();
    }

    struct SelectedFramePartition
    {
        QStringList included;
        QStringList excluded;
    };

    // Defined in RegistrationTab.cpp.
    void applyCardContentMargins();
    explicit Impl(RegistrationTab* parent, IntegrationDataModel* m, QUndoStack* undo);
    void applyBackground(QWidget* widget = nullptr, bool useBase = false);
    void applyThemeStyling();
    void configureFrameQualityScrollArea();
    bool executeMetricsDb(const std::function<void(QSqlDatabase&)>& fn);
    void setupPerGroupButtons();
    void setupTables();
    void configureLayoutSizing();
    void setupChartWidget();
    void setupDebounceTimer();
    void setupConnections();
    void onModelSelectionChanged();

    // Defined in RegistrationTabTables.cpp.
    void updateChartData();
    [[nodiscard]] const WorkflowManifest* currentRegistrationAnalysisManifest();
    std::vector<RegistrationGroupEntry> collectRegistrationGroups() const;
    void setEmptyMessage(QTableWidget* table, const QString& message);
    void updateGroupTable();
    const RegistrationGroupEntry* selectedGroup() const;
    [[nodiscard]] QString qualityFilterKeyForFrame(const FrameId& frameId, const bool hasAnyNamedFilter) const;
    [[nodiscard]] FrameScoreContext buildFrameScoreContext(const RegistrationGroupEntry& group) const;
    [[nodiscard]] double qualityScoreForFrame(const FrameId& frameId, const FrameQualityMetrics& metrics,
                                              const FrameScoreContext& context) const;
    void updateFrameTable();
    std::unordered_map<QString, MetricRow> loadRegistrationMetrics(const RegistrationGroupEntry& group);
    int getDesiredVersion(const RegistrationGroupKey& key) const;
    void selectFrameInQualityTable(const QString& canonicalPath);

    // Defined in RegistrationTabActions.cpp.
    void showFrameContextMenu(const QPoint& pos);
    void showGroupContextMenu(const QPoint& pos);
    void updateThresholdPreview();
    void openSettingsDialogForSelectedGroup();
    /// Picks the reference frame for the settings dialog from the group's registration metrics, offering to run the
    /// registration first when they are missing or incomplete.
    void continueAutoReference(RegistrationSettingsDialog* dialog, const RegistrationGroupKey& group_key_struct,
                               const std::shared_ptr<AutoReferenceContext>& autoRefCtx,
                               const QString& machine_key_label, const QString& machine_key_str);
    /// Runs the group's registration for the settings dialog's Auto Reference, and picks the reference frame once its
    /// metrics are complete.
    void runRegistrationForAutoReference(RegistrationSettingsDialog* dialog,
                                         const RegistrationGroupKey& group_key_struct,
                                         const std::shared_ptr<AutoReferenceContext>& autoRefCtx,
                                         const QString& machine_key_label, const QString& machine_key_str);
    void saveSettingsFromDialog(const RegistrationSettingsDialog& dialog, const GroupKey& machine_key);
    void applySettingsToAllGroups(const RegistrationSettingsDialog& dialog);
    void applyQualityThresholds();
    void detectOutliers();
    [[nodiscard]] QualityThresholdConfig currentQualityThresholdConfig() const;
    void syncThresholdEditorStates();
    void updateChartThresholds();
    std::optional<FrameId> selectedSingleFrameId() const;
    SelectedFramePartition selectedFramePathsByExclusion() const;
    void setFramesExcluded(const QStringList& canonicalPaths, bool excluded);
    void resetExcludedFrames();
    void clearRegistrationForSelectedGroup();
    [[nodiscard]] bool groupNeedsRegistrationWork(const RegistrationGroupEntry& selectedGroup,
                                                  QStringList& reasons_out);
    void updateRunAllButton();
    void updateRunGroupButton(const RegistrationGroupEntry* group);
    void updateClearGroupButton(const RegistrationGroupEntry* group);
    void updateResetExcludedButton(const RegistrationGroupEntry* group);
    void updateActionButtons();
};
