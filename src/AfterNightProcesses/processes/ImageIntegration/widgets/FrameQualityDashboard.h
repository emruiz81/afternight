// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <cstdint>

class IntegrationDataModel;
class QDoubleSpinBox;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QEvent;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class QUndoStack;
struct FrameInfo;

class AFTERNIGHTPROCESSES_API FrameQualityDashboard : public QWidget
{
    W_OBJECT(FrameQualityDashboard)

public:
    enum class ExclusionMode
    {
        Global,
        StackOnly
    };

    struct WeightingPreviewConfig
    {
        QString weightingMode; // e.g. "None", "Noise", "Number of stars", "Weighted FWHM", "Number of images"
        QString starCountScaling;
        double exponentialAlpha = 2.0;
        double minWeightFloor = 0.01;
        /// The light the group's stack weighs against, by canonical path (`stackingReferenceFrame`): the width
        /// weighting penalizes a frame for the stars it lacks against it. Empty when none is named, and the preview
        /// then weighs against its first frame by identity, as the stacking stage does.
        QString referenceFrame;
    };

    explicit FrameQualityDashboard(IntegrationDataModel* model, QWidget* parent = nullptr,
                                   QUndoStack* undoStack = nullptr);
    ~FrameQualityDashboard() override;

public:
    void refresh();

    // Quality-based weighting preview.
    void setRegistrationGroupFilteringEnabled(bool enabled);
    void setFrameIdFilter(const QStringList& canonicalFrameIds);
    void clearFrameIdFilter();
    void setExclusionMode(ExclusionMode mode);
    void setStackExclusionGroupKey(const GroupKey& key);
    void setWeightingPreviewConfig(const WeightingPreviewConfig& config);
    /// Shows the weights the group's stack applies, or a banner saying it applies none.
    ///
    /// @param enabled whether the stack applies a frame weighting.
    /// @param disabledReason why it applies none, as the banner says it; empty for a group that stores none.
    void setWeightingEnabled(bool enabled, const QString& disabledReason = {});
    [[nodiscard]] bool weightingEnabled() const;
    [[nodiscard]] bool hasColumn(const QString& name) const;

    // UX helper: select + scroll + briefly flash a specific frame row.
    void highlightFrame(const QString& canonicalPath);
    // UX helper: navigate to Frame Sources -> Lights and select a frame.
    void showInLightsTree(const QString& canonicalPath);

    void calculateWeights();
    [[nodiscard]] double weightFor(int row) const;
    void setManualWeight(int row, double weight);
    [[nodiscard]] bool isManuallyOverridden(int row) const;
    [[nodiscard]] double effectiveExposureSeconds() const;
    [[nodiscard]] QWidget* weightHistogram() const;

    /// Get the computed per-filter statistics (available after refresh())
    [[nodiscard]] const QMap<QString, FilterStatistics>& filterStatistics() const;

private:
    void applyThemeStyling();
    void onFramesAdded();
    void onDetectOutliersClicked();
    void onExcludeSelectedClicked();
    void onIncludeSelectedClicked();
    void onWeightingToggled(bool enabled);
    void onWeightItemChanged(QTableWidgetItem* item);
    void updateEffectiveExposureLabel();
    void updateExclusionUiText();

    /// Reads how many frames each light at `paths` combines from its header on the thread pool, and computes the
    /// preview again when the counts land; does nothing while a read is under way.
    void readCombinedFrameCountsOf(const QStringList& paths);

    void computeFilterStatistics(const std::vector<FrameInfo>& frames);
    [[nodiscard]] QString buildMetricTooltip(const QString& metricName, double absoluteValue, const QString& filterKey,
                                             bool higherIsBetter) const;
    [[nodiscard]] bool isStackExclusionMode() const;
    [[nodiscard]] bool isFrameExcludedFromCurrentStack(const FrameInfo& frame) const;
    [[nodiscard]] QString exclusionStateText(const FrameInfo& frame) const;

    IntegrationDataModel* m_model = nullptr;
    QUndoStack* m_undoStack = nullptr;
    QLabel* m_helpLabel = nullptr;
    QLabel* m_banner = nullptr;
    QLabel* m_weightingBanner = nullptr;
    QLabel* m_outlierHelpLabel = nullptr;
    QTableWidget* m_table = nullptr;
    QDoubleSpinBox* m_sigmaSpinBox = nullptr;
    QPushButton* m_detectButton = nullptr;
    QPushButton* m_excludeButton = nullptr;
    QPushButton* m_includeButton = nullptr;

    QLabel* m_effectiveExposureLabel = nullptr;
    QWidget* m_weightHistogram = nullptr;

    WeightingPreviewConfig m_weightingConfig{};
    bool m_filterByRegistrationGroup = false;
    bool m_frameIdFilterActive = false;
    QSet<QString> m_frameIdFilter;
    ExclusionMode m_exclusionMode = ExclusionMode::Global;
    GroupKey m_stackExclusionGroupKey;
    bool m_weightingEnabled = false;
    QString m_weightingDisabledReason;
    QHash<QString, double> m_computedWeights;
    QHash<QString, double> m_manualWeights;
    double m_lastEffectiveExposureSeconds = 0.0;
    /// Whether a weights preview the session had no thread for is to be asked for again.
    bool m_weightsPreviewRetryPending = false;
    /// How many frames each light combines, by canonical path, as its header states it: what the Number of images
    /// preview weighs by, read off the GUI thread and kept until the dashboard is refreshed.
    QHash<QString, std::uint32_t> m_combinedFrameCounts;
    /// Why the lights' headers could not be read; empty when they could.
    QString m_combinedFrameCountsFailure;
    /// Whether a read of the lights' headers is under way.
    bool m_combinedFrameCountsReading = false;
    /// Moves when the dashboard is refreshed, so a read begun before it is let go of.
    quint64 m_combinedFrameCountsGeneration = 0;

    // Filter-relative statistics
    QMap<QString, FilterStatistics> m_filterStats;
    QHash<QString, QString> m_frameFilterMap; // frameId -> filter name
    bool m_applyingThemeStyling = false;

protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
};
