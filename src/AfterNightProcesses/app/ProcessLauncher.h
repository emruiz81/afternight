// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The launcher every registered process opens through: a member per process, and the guards a single-image process is
/// opened under.
///
/// A launcher acts for the descriptor it was invoked through and reads its guards from it - the process's name and
/// whether it needs a view to itself - through the one reading of a view the menu's action state uses
/// (`launchGuardInputFor`), so the launcher and the application's guards name a process alike.
///
/// The members that open a process need that process's header, so they are defined apart from the guards, in
/// `app/ProcessEntryPoints.cpp`, which compiles with every process; this unit holds the guards, the notifications and
/// the two mask actions, which open no process.
#pragma once

#include "AfterNightProcessesExport.h"

#include "app/ProcessRegistry.h"

#include <functional>
#include <string>

#include <QString>

class ImageViewControl;

namespace an::ui
{

/// What the launcher reads of the view a single-image process would open on.
struct SingleImageLaunchState
{
    /// The active view, or null when there is none.
    ImageViewControl* activeControl = nullptr;

    /// What the descriptor's guards read of it.
    process_registry::LaunchGuardInput input;
};

/// Where a refused launch is reported.
struct LaunchNotifications
{
    std::function<void(const QString& title, const QString& message)> notifyError;
    std::function<void(const QString& title, const QString& message)> notifyWarning;
};

/// Opens a single-image process on the active view when the guards its descriptor states admit it, and reports why
/// not otherwise: a missing image as an error, and a channel count, a sequence or an attached process the descriptor
/// refuses as a warning, titled by the descriptor's process name.
///
/// @param descriptor the process's descriptor.
/// @param state the active view and what the guards read of it.
/// @param notifications where a refusal is reported; either may be empty.
/// @param launch opens the process on the view; may be empty.
/// @return whether the guards admitted the launch.
inline bool launchSingleImageProcess(const process_registry::ProcessDescriptor& descriptor,
                                     const SingleImageLaunchState& state, const LaunchNotifications& notifications,
                                     const std::function<void(ImageViewControl*)>& launch)
{
    const process_registry::LaunchGuardResult guard =
        process_registry::evaluateSingleImageLaunchGuards(descriptor, state.input);
    if (!guard.ok())
    {
        const auto& notify = guard.reason == process_registry::LaunchGuardFailureReason::MissingActiveImage
                                 ? notifications.notifyError
                                 : notifications.notifyWarning;
        if (notify)
        {
            notify(QString::fromStdString(guard.title), QString::fromStdString(guard.message));
        }
        return false;
    }

    if (launch)
    {
        launch(state.activeControl);
    }

    return true;
}

class AFTERNIGHTPROCESSES_API ProcessLauncher
{
public:
    /// A launcher acting for no descriptor: a top-level or non-interactive launch reads none, and a single-image launch
    /// is refused.
    ProcessLauncher() = default;

    /// A launcher acting for a registered descriptor: a single-image launch reads that descriptor's guards.
    ///
    /// @param descriptorId the id the descriptor is registered under.
    explicit ProcessLauncher(std::string descriptorId);

    /// The id of the descriptor the launcher acts for; empty for none.
    [[nodiscard]] const std::string& descriptorId() const noexcept;

    void executeExtractFrame() const;
    void extractSequence() const;

    void adjustBrightnessAndContrast() const;
    void gammaCorrection() const;
    void exposureCorrection() const;
    void adjustHSL() const;
    void adjustLCh() const;

    void arcSinhStretch() const;
    void histogramTransformation() const;
    void generalizedHyperbolicTransformation() const;
    void curvesTransformation() const;

    void starnet() const;
    void starmaskRecombination() const;
    void profiledStarmaskRecombination() const;
    void executeStarReduction() const;
    void executeStarMask() const;
    void executeStarnetStarMask() const;

    void executeSCNR() const;
    void executeMedianFilter() const;
    void executeDeepSNR() const;
    void executeNLBayesFilter() const;
    void executeWaveletDenoiseFilter() const;
    void executePatchPCAFilter() const;
    void executeStatisticalStretch() const;

    void executeLocalHistogramEqualizationFilter() const;
    void executeLocalContrastEnhancementFilter() const;
    void executeDarkStructureEnhance() const;
    void executeSharpenAlphaFilter() const;
    void executeGaussianBlurFilter() const;
    void executeHighPassFilter() const;

    void executeChannelComposition() const;
    void executeImageDebayer() const;
    void executeMergeCFA() const;
    void executeSplitCFA() const;

    void executeResize() const;
    void executeIntegerResample() const;
    void executeCFAIntegerResample() const;
    void executeCrop() const;
    void executeRotate() const;

    void executePixelMath() const;
    void executeBinarize() const;
    void executeMaskDrawing() const;
    void executeRangeSelection() const;

    void executeCLAHE() const;
    void executeColorSaturation() const;

    void executeColorCalibration() const;
    void executePhotometricColorCalibration() const;
    void executeSpectrophotometricColorCalibration() const;
    void executeBackgroundNeutralization() const;
    void executeLinearMatch() const;

    void executeBackgroundExtraction() const;
    void executeAutoGradientRemoval() const;

    void executeMultiscaleTransform() const;

    void executeMorphologicalTransformation() const;
    void executeTightenEdges() const;
    void executeGRAYCstoration() const;
    void executeCosmeticCorrection() const;
    void executeBandingReduction() const;
    void executeBilateralFilter() const;

    void executeStarProfilerV1() const;
    void executeStarProfilerV2() const;

    void executeUnsharpMask() const;
    void executeNoiseGenerator() const;
    void executeTotalVariation() const;

    void executeImageCompare() const;
    void executeImageBlend() const;
    void executeImageListAnalyzer() const;

    void executeLRGBCombination() const;
    void executeNBRGBCombination() const;
    void executeNBColorMapper() const;
    void executeNarrowbandPaletteComposer() const;
    void executeColorize() const;
    void executeSelectMask() const;
    void executeRemoveMask() const;

    void executeImageCalibration() const;
    void executeImageStacking() const;
    void executeImageRegistration() const;
    void executeImageIntegration() const;
    void executeImageIntegrationLite() const;

    /// Opens StarNet over a view to make the starless image Star Reduction reduces against, configured as the reduction
    /// wants it - no star mask, its stars unscreened, its window closing once the image is made - and hands the
    /// starless view it makes to `onStarless`. StarNet's own guards decide whether it opens; nothing opens otherwise.
    ///
    /// @param target the view StarNet runs over.
    /// @param onStarless what receives the starless view StarNet makes.
    static void openIntegratedStarlessGeneration(ImageViewControl* target,
                                                 std::function<void(ImageViewControl*)> onStarless);

private:
    /// Opens a single-image process on the active view when the guards of the descriptor the launcher acts for admit
    /// it; reports a launcher acting for no registered descriptor as an error and opens nothing.
    void launchSingleImageProcess(const std::function<void(ImageViewControl*)>& launch) const;
    void launchTopLevelProcess(const std::function<void()>& launch) const;
    LaunchNotifications notifications() const;

    std::string m_descriptorId;
};

} // namespace an::ui
