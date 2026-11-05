// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <QColor>
#include "StackingSettingsDialog.h"
#include <verdigris/wobjectdefs.h>

class QGridLayout;

/**
 * @brief A specialized widget to visualize sub-pixel dithering coverage.
 */
class AFTERNIGHTPROCESSES_API DrizzleCoverageWidget : public QWidget
{
    W_OBJECT(DrizzleCoverageWidget)
public:
    explicit DrizzleCoverageWidget(QWidget* parent = nullptr);
    void setCoverage(double coverage, int filledBins, int totalBins);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_coverage = 0.0;
    int m_filledBins = 0;
    int m_totalBins = 16;
};

/**
 * @brief Modern dialog for displaying and applying stacking recommendations.
 */
class AFTERNIGHTPROCESSES_API StackingRecommendationDialog : public QDialog
{
    W_OBJECT(StackingRecommendationDialog)
public:
    explicit StackingRecommendationDialog(StackingSettingsDialog::FrameType frameType, const QString& groupLabel,
                                          int frameCount, const StackingRecommendation& recommendation,
                                          const DrizzleAnalysis& drizzleAnalysis,
                                          const StackingSettings& currentSettings, QWidget* parent = nullptr);

    bool isApplyRequested() const
    {
        return m_applyRequested;
    }
    StackingRecommendation getRecommendation() const
    {
        return m_recommendation;
    }

private:
    void setupUi();
    void addComparisonRow(QGridLayout* layout, int row, const QString& label, const QString& current,
                          const QString& recommended, bool different);
    bool matchesRecommendation(const StackingRecommendation& rec) const;

    StackingSettingsDialog::FrameType m_frameType;
    QString m_groupLabel;
    int m_frameCount;
    StackingRecommendation m_recommendation;
    DrizzleAnalysis m_drizzleAnalysis;
    StackingSettings m_currentSettings;
    bool m_applyRequested = false;

    // Theming
    QColor m_accentColor;
    QColor m_goodColor;
    QColor m_warnColor;
    QColor m_cardBg;
    QColor m_cardBorder;
    QColor m_mutedTextColor; ///< Body/explanation text (AA-contrast in both themes)
    QColor m_subtleTextColor; ///< Secondary info and de-emphasized labels
};
