// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The photometric colour calibration window: three channel gains from the Gaia colours of the stars a plate-solved
/// RGB image holds, and a neutral sky.
///
/// **The window decides what may be calibrated; the engine calibrates.** A target is offered when it is three-channel
/// RGB, not declared nonlinear, not taken through a narrowband filter and plate solved, and the Gaia catalog is
/// installed (`photometricTargetProblem`, `inspectGaiaCatalogDirectory`); the banner says which of those is missing
/// and offers the plate solve or the catalog settings. An Apply snapshots the dialog, the target's solution and its
/// mask on the GUI thread and edits the target's document: on the edit's worker, holding the document's writer, the
/// catalog's stars inside the field and their colours are read (`GaiaCatalogSource::regionWithColors`), the image's
/// working copy is calibrated by `calibratePhotometricColor` with a fixed star finder, the result is brought back to
/// the image's representation, the mask weighs it, and its metadata records the calibration. The history records the
/// step before the view shows it; a document changed or closed under the run, Cancel or closing the window stops the
/// run and publishes nothing.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ImageCropController.h"
#include "controls/ImageProcessControl.h"
#include "processes/PhotometricColorCalibration/PhotometricColorCalibrationUiBuilder.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightEngine/Algorithms/Photometry/PhotometricColorCalibration.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>

#include <QRect>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

class ImageViewControl;

namespace an::ui::processes
{

/// What one photometric calibration is asked to do, snapshotted on the GUI thread.
struct PhotometricCalibrationSettings
{
    /// The faintest Gaia G magnitude matched.
    double magnitudeLimit = 14.0;

    /// The largest separation at which a detection and a catalog star match, in arcseconds.
    double matchRadiusArcseconds = 5.0;

    /// Whether the sky is measured and neutralized.
    bool neutralizeBackground = true;

    /// The rectangle the sky is measured in, or empty for the whole image.
    QRect backgroundRegion;
};

/// How the photometric calibration finds stars in each channel: a fixed star finder - a search radius of five pixels,
/// one sigma above the sky, a roundness of at least 0.4, Gaussian fits with the shape checks relaxed.
///
/// @return the detection request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarDetectionRequest photometricStarDetection();

/// The engine request one calibration runs with: two thousand stars a channel, three-pixel channel pairing and a
/// ten-sample floor, and the dialog's match, magnitude and sky.
///
/// @param settings what the dialog asked.
/// @param filterName the image's filter, which refuses a narrowband frame by name.
/// @return the request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::PhotometricColorCalibrationRequest
photometricCalibrationRequest(const PhotometricCalibrationSettings& settings, std::string_view filterName);

} // namespace an::ui::processes

/// Draws the stars a calibration matched over the target: green where the catalog gave the star's colour, yellow
/// where the neutral default stood in.
class AFTERNIGHTPROCESSES_API PccMatchedStarOverlay final : public ImageViewWidgetDisplayModule
{
public:
    /// Shows the matched stars of a calibration.
    void setMatchedSamples(std::vector<an::PhotometricColorSample> samples);

    /// Shows none.
    void clearMatchedSamples();

    /// The stars shown now.
    [[nodiscard]] std::vector<an::PhotometricColorSample> matchedSamples() const;

private:
    void onDraw(QPainter& painter) override;

    mutable std::mutex m_mutex;
    std::vector<an::PhotometricColorSample> m_samples;
};

class AFTERNIGHTPROCESSES_API PhotometricColorCalibrationProcess : public ImageProcessControl,
                                                                   public ImageAreaSelectorControl
{
public:
    explicit PhotometricColorCalibrationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;
    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;
    void selectionAreaUpdated() override;
    void selectionAreaChanged() override;
    void newSelectionFinished() override;

    /// Runs the calibration as the Apply button does.
    void applyCalibration();

    /// What the dialog asks of a calibration now.
    [[nodiscard]] an::ui::processes::PhotometricCalibrationSettings settings() const;

    /// The overlay of the stars the last calibration matched, or null while the window shows none.
    [[nodiscard]] const PccMatchedStarOverlay* matchedStarOverlay() const;

    ~PhotometricColorCalibrationProcess() override;

protected:
    /// Called on the GUI thread as a run is issued, with the stop the window asks it to stop through; a case that stops
    /// its own run overrides it.
    virtual void calibrationStarting(std::stop_source& stop) {}

private:
    struct RunOutcome;

    void refreshTheme() override;
    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    void on_dialog_box_button_clicked(QAbstractButton* button);
    void finalizeCalibration(const RunOutcome& outcome, bool shown);
    void onBackgroundRoiGroupBoxClicked(bool checked);
    void onBackgroundRoiSelectionButtonClicked(bool checked);
    void onBackgroundRoiLeftValueChanged(int value);
    void onBackgroundRoiTopValueChanged(int value);
    void onBackgroundRoiWidthValueChanged(int value);
    void onBackgroundRoiHeightValueChanged(int value);

    void updateAvailabilityState();
    void onShowMatchedStarsToggled(bool checked);
    void openPlateSolveSettings();
    void triggerPlateSolve();
    void clearCalibrationResult(bool clearStatusLabel);

    void attachOverlayToMainView();
    void detachOverlay();
    void updateBackgroundRoiData();
    void updateSelectionArea();
    void destroyCropController();
    void resetBackgroundRoiValues();
    void closeEvent(QCloseEvent* event) override;

    an::ui::processes::PhotometricColorCalibrationUiElements m_ui;
    std::unique_ptr<PccMatchedStarOverlay> m_starOverlay;
    std::unique_ptr<ImageCropController> m_CropController;
    std::shared_ptr<std::stop_source> m_runStop;
    bool m_hasCalibrationResult = false;
    bool m_IgnoreLastCropMoveEvent = false;
};
