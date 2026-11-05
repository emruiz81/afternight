// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QVariantList>
#include <verdigris/wobjectdefs.h>

class QLabel;
class QTimer;
class QSlider;
class QTableWidget;
class QPushButton;
class ImageViewControl;
class QCloseEvent;
class QKeyEvent;
class QResizeEvent;

namespace an::ui
{
class AnCardWidget;
class ComparisonViewWidget;
} // namespace an::ui

struct BlinkComparatorResult
{
    QStringList excludedPaths;
};

/**
 * @class BlinkComparatorDialog
 * @brief Dialog for comparing multiple images by blinking through them.
 *
 * Supports:
 * - Play/Pause with configurable speed
 * - Manual Previous/Next navigation
 * - Keyboard shortcuts: Space (play/pause), Left/Right arrows (prev/next), Escape (close)
 */
class AFTERNIGHTPROCESSES_API BlinkComparatorDialog : public QDialog
{
    W_OBJECT(BlinkComparatorDialog)

public:
    explicit BlinkComparatorDialog(const QStringList& files, QWidget* parent = nullptr, bool enableImageLoading = true);
    explicit BlinkComparatorDialog(const QStringList& files, const QStringList& labels, QWidget* parent = nullptr,
                                   bool enableImageLoading = true);
    explicit BlinkComparatorDialog(const QStringList& files, const QStringList& labels, const QStringList& contexts,
                                   QWidget* parent = nullptr, bool enableImageLoading = true);
    ~BlinkComparatorDialog() override;

    // Expose for testing
    [[nodiscard]] int currentIndex() const
    {
        return m_currentIndex;
    }
    [[nodiscard]] int intervalMs() const
    {
        return m_intervalMs;
    }
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] int comparisonTargetIndex() const
    {
        return m_historyTargetIndex;
    }

    enum class BlinkMode
    {
        Single,
        SideBySide,
        Overlay,
        Difference
    };
    enum class SortCriteria
    {
        Filename,
        Quality,
        FWHM,
        Stars
    };

    void setMode(BlinkMode mode);
    [[nodiscard]] BlinkMode currentMode() const
    {
        return m_mode;
    }

    [[nodiscard]] ImageViewControl* leftPanel() const;
    [[nodiscard]] ImageViewControl* rightPanel() const;
    [[nodiscard]] an::ui::ComparisonViewWidget* comparisonView() const;

    void setOpacity(double opacity);
    [[nodiscard]] double overlayOpacity() const
    {
        return m_overlayOpacity;
    }

    void excludeCurrentFrame();
    [[nodiscard]] bool isExcluded(int index) const;

    [[nodiscard]] bool supportsSortCriteria(SortCriteria criteria) const;
    void sortBy(SortCriteria criteria, Qt::SortOrder order);
    [[nodiscard]] int ratingFor(int index) const;
    [[nodiscard]] BlinkComparatorResult comparisonResult() const;
    void setHistoryRows(const QVariantList& rows);

    // For testing access to thumbnail strip (using QWidget as base, cast in tests if needed)
    [[nodiscard]] QWidget* thumbnailStrip() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void done(int result) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    [[nodiscard]] QString canonicalPathForIndex(int index) const;
    [[nodiscard]] QString currentCanonicalPath() const;
    [[nodiscard]] QString displayLabelForIndex(int index) const;
    [[nodiscard]] QString displayHeaderForIndex(int index) const;
    [[nodiscard]] QString displayContextForIndex(int index) const;
    [[nodiscard]] bool usesHistoryComparisonSelection() const;
    [[nodiscard]] int activeRightIndex() const;
    [[nodiscard]] QVariantList buildSourceFrameRows() const;

    void normalizeLabels();
    void normalizeContexts();
    void refreshThumbnailLabels();
    void updateImageLabels(int rightIndex);
    void updateContextCards(int rightIndex);
    void restoreWindowSize();
    void rememberWindowSize() const;
    void showNextImage();
    void showPreviousImage();
    void updateImage();
    void advanceHistoryTarget(int step);
    void setHistoryTargetIndex(int index);
    void setCurrentFrameIndexFromTable(int index);
    void populateHistoryTable();
    void updateHistoryTableSelection();
    void applyHistoryTableHighlights();
    void togglePlayPause();
    void onSpeedChanged(int sliderValue);

    int sliderValueToInterval(int sliderValue) const;
    int intervalToSliderValue(int intervalMs) const;

    QStringList m_files;
    QStringList m_labels;
    QStringList m_contexts;
    bool m_enableImageLoading = true;
    int m_currentIndex = 0;
    QTimer* m_timer = nullptr;

    an::ui::ComparisonViewWidget* m_comparisonView = nullptr;

    QLabel* m_leftImageLabel = nullptr;
    QLabel* m_rightImageLabel = nullptr;
    an::ui::AnCardWidget* m_leftContextCard = nullptr;
    an::ui::AnCardWidget* m_rightContextCard = nullptr;
    QLabel* m_leftContextLabel = nullptr;
    QLabel* m_rightContextLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_prevButton = nullptr;
    QPushButton* m_nextButton = nullptr;
    QSlider* m_speedSlider = nullptr;

    // New UI elements
    QWidget* m_thumbnailStrip = nullptr;
    QTableWidget* m_historyTable = nullptr;
    QVariantList m_historyRows;
    bool m_showingExecutionHistoryRows = false;
    int m_historyReferenceIndex = -1;
    int m_historyTargetIndex = -1;
    QSize m_lastVisibleWindowSize;

    int m_intervalMs = 500;
    bool m_syncingHistoryTable = false;

    BlinkMode m_mode = BlinkMode::Single;
    double m_overlayOpacity = 0.5;

    QSet<QString> m_excludedPaths;
    QMap<QString, int> m_ratingsByPath;

    static constexpr int kMinInterval = 50;
    static constexpr int kMaxInterval = 2000;
};
