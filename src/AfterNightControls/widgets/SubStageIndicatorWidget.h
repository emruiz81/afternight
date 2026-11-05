// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QVector>
#include <verdigris/wobjectdefs.h>

/**
 * @brief Widget displaying staged pipeline sub-stages with progress indicators
 *
 * Shows the progression through pipeline stages:
 * Calibrating → Detecting Stars → Registering → Aligning → Stacking
 *
 * Each sub-stage shows:
 * - State indicator (pending/active/completed/skipped)
 * - Image count (e.g., "45/100")
 * - Optional elapsed time
 */
class AFTERNIGHTCONTROLS_API SubStageIndicatorWidget : public QFrame
{
    W_OBJECT(SubStageIndicatorWidget)

public:
    /// Sub-stage types for the staged pipeline
    enum class SubStage
    {
        Calibration,
        StarDetection,
        Registration,
        Alignment,
        Stacking,
        Count ///< Number of sub-stages (for iteration)
    };

    /// Sub-stage state
    enum class SubStageState
    {
        Pending, ///< Not yet started
        Active, ///< Currently processing
        Completed, ///< Finished successfully
        Skipped, ///< Intentionally skipped (e.g., alignment for drizzle)
        Failed ///< Failed with error
    };

    /// Sub-stage statistics
    struct SubStageStats
    {
        int processedCount = 0; ///< Images processed so far
        int totalCount = 0; ///< Total images to process
        int failedCount = 0; ///< Failed image count
        float progress = 0.0f; ///< Progress 0.0-1.0
        int elapsedMs = 0; ///< Elapsed time in milliseconds
    };

    explicit SubStageIndicatorWidget(QWidget* parent = nullptr);
    ~SubStageIndicatorWidget() override = default;

    /// Set the state of a sub-stage
    void setSubStageState(SubStage stage, SubStageState state);

    /// Update sub-stage statistics (processed count, total, progress)
    void setSubStageStats(SubStage stage, const SubStageStats& stats);

    /// Set whether the alignment stage is skipped (for drizzle mode)
    void setAlignmentSkipped(bool skipped);

    /// Reset all sub-stages to pending state
    void resetAllStages();

    /// Set in-memory calibration mode.
    /// When enabled, the Calibration and StarDetection stages are visually merged,
    /// displaying "Calib+Detect" instead of separate "Calibrate" and "Detect" boxes.
    /// This reflects the combined calibration+star detection pipeline stage.
    void setInMemoryCalibrationMode(bool enabled);

    /// Check if in-memory calibration mode is enabled
    [[nodiscard]] bool isInMemoryCalibrationMode() const
    {
        return m_inMemoryCalibrationMode;
    }

    /// Get the state of a sub-stage
    [[nodiscard]] SubStageState getSubStageState(SubStage stage) const;

    /// Get the statistics of a sub-stage
    [[nodiscard]] const SubStageStats& getSubStageStats(SubStage stage) const;

    /// Check if any sub-stage is currently active
    [[nodiscard]] bool hasActiveStage() const;

    /// Get currently active sub-stage (or Calibration if none active)
    [[nodiscard]] SubStage getActiveStage() const;

    /// Get display name for a sub-stage
    [[nodiscard]] static QString getSubStageName(SubStage stage);

    /// Get short name for a sub-stage (for compact display)
    [[nodiscard]] static QString getSubStageShortName(SubStage stage);

    /// Return the color associated with a given sub-stage state (uses design tokens)
    [[nodiscard]] QColor stateColor(SubStageState state) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* ev) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    struct SubStageData
    {
        SubStageState state = SubStageState::Pending;
        SubStageStats stats;
    };

    void updateTooltips();
    QColor getStateColor(SubStageState state) const;
    QString getStateIcon(SubStageState state) const;
    void drawSubStage(QPainter& painter, int index, const QRect& rect);
    void drawArrow(QPainter& painter, int x, int centerY);

    QVector<SubStageData> m_stages;
    bool m_alignmentSkipped = false;
    bool m_inMemoryCalibrationMode = false; ///< When true, Calibration+StarDetection shown as merged

    static constexpr int kSubStageCount = static_cast<int>(SubStage::Count);
    static constexpr int kArrowWidth = 16;
    static constexpr int kMinStageWidth = 90;
    static constexpr int kStageHeight = 50;
    static constexpr int kVerticalPadding = 4;
};
