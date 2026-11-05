// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The plate-solve window over an image view: the acquisition hints the image's cards state, filled into fields a
/// person can correct, the solve run on the thread pool against the image the view shows, the solved field's figures,
/// and the solution written into the image - or withdrawn from it - as an edit the history records.
#pragma once

#include "AfterNightControlsExport.h"

#include "controls/ImageViewControlListener.h"

#include "app/PlateSolveTypes.h"
#include "components/AnInfoBanner.h"
#include "session/ImageDocument.h"

#include <QDialog>
#include <verdigris/wobjectdefs.h>

#include <cstdint>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QCloseEvent;
class QEvent;
class QGroupBox;
class QObject;
class QSpinBox;

namespace an::ui
{
class AnCardWidget;
class AnCollapsibleSection;
class AnInfoBanner;
class PlateSolveService;
} // namespace an::ui

class ImageViewControl;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API PlateSolveControl : public QDialog, public ImageViewControlListener
{
    W_OBJECT(PlateSolveControl)

public:
    explicit PlateSolveControl(ImageViewControl* viewControl, QWidget* parent = nullptr);
    ~PlateSolveControl() override;

    /// Whether a solve runs.
    [[nodiscard]] bool isSolving() const;

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onImageRenamed(ImageViewControl* ctrl) override;
    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void syncFixedWindowSize();
    void cleanupActiveInputFile();
    [[nodiscard]] an::ui::PlateSolveSettings readSettings() const;
    [[nodiscard]] QString currentImagePath() const;
    [[nodiscard]] bool canSolveCurrentImage(QString* errorMessage = nullptr) const;
    void refreshForCurrentImage();
    void populateHintsFromMetadata();
    void updateHintWarning();
    void applyCoordinateHints(double ra_deg, double dec_deg);
    void openCatalogSearch();
    void setBusyState(bool busy);
    void setStatusMessage(const QString& message, const QString& tone = QStringLiteral("secondary"));
    void showBanner(const QString& message, an::ui::AnInfoBanner::Tone tone);
    void clearBanner();
    void clearSolvedDisplay();
    void populateSolvedDisplayFromMetadata();
    [[nodiscard]] std::vector<an::CodecCard> currentImageCards() const;
    void startSolve();
    void cancelOrClose();
    void resetHints();
    void clearAstrometry();
    void updateClearAstrometryButtonState();
    [[nodiscard]] bool isSolverConfigured() const;
    void updateSolverConfiguredState();
    void openSolverSettings();
    void onSolveFinished(an::ui::PlateSolveResult result);
    bool applySolvedFocalLength(const an::ui::PlateSolveResult& result);
    void updateSolvedDisplay(const an::ui::PlateSolveResult& result);
    void writeWcsToMetadata(const an::ui::PlateSolveResult& result);

    ImageViewControl* m_viewControl = nullptr;
    an::ui::PlateSolveService* m_service = nullptr;
    an::ui::AnInfoBanner* m_banner = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QGroupBox* m_resultCard = nullptr;
    QLabel* m_raLabel = nullptr;
    QLabel* m_decLabel = nullptr;
    QLabel* m_scaleLabel = nullptr;
    QLabel* m_rotationLabel = nullptr;
    QLabel* m_fovLabel = nullptr;
    QLabel* m_solverLabel = nullptr;
    QLabel* m_sipLabel = nullptr;
    QCheckBox* m_writeWcsCheckBox = nullptr;
    QPushButton* m_solveButton = nullptr;
    QPushButton* m_resetButton = nullptr;
    QPushButton* m_clearAstrometryButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    an::ui::AnCollapsibleSection* m_hintsSection = nullptr;
    QSpinBox* m_raHoursSpin = nullptr;
    QSpinBox* m_raMinutesSpin = nullptr;
    QDoubleSpinBox* m_raSecondsSpin = nullptr;
    QSpinBox* m_decDegreesSpin = nullptr;
    QSpinBox* m_decMinutesSpin = nullptr;
    QDoubleSpinBox* m_decSecondsSpin = nullptr;
    QComboBox* m_decHemisphereCombo = nullptr;
    QDoubleSpinBox* m_hintFocalLengthSpin = nullptr;
    QDoubleSpinBox* m_hintPixelSizeSpin = nullptr;
    QLabel* m_hintWarningLabel = nullptr;
    QPushButton* m_findObjectButton = nullptr;
    QPushButton* m_configureButton = nullptr;
    an::ui::AnInfoBanner* m_solverNotConfiguredBanner = nullptr;
    bool m_isSolving = false;
    bool m_closeOnCancelFinish = false;
    bool m_syncingWindowSize = false;
    QString m_activeInputPath;
    uint64_t m_solveGeneration = 0;
    uint64_t m_activeSolveGeneration = 0;
    bool m_hasDisplayedSolve = false;

    /// The document's revision the running solve reads; its answer is applied only to that revision.
    an::ui::DocumentRevision m_solveRevision;
};

} // namespace an::ui
