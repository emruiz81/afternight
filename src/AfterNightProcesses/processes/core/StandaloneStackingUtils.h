// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file StandaloneStackingUtils.h
 * @brief Shared helpers for standalone stacking grouping and weighting.
 *
 * A frame is the integration's record of it - its filter, its capture time in UTC, its exposure - read from its header
 * by the same import, so the standalone stacking groups a set as the integration would. Its weights are the engine's
 * weighting sources over the measurements the registration database records (`an::StackFrameMeasurement`).
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ImageListTableHelpers.h"
#include "processes/ImageIntegration/model/FrameId.h"

#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>

#include <QDateTime>
#include <QString>

#include <optional>
#include <vector>

namespace an::ui
{
/**
 * @brief Grouping toggles exposed by the standalone stacking UI.
 */
struct StandaloneStackingGroupingOptions
{
    bool byFilter = true;
    bool bySession = false;
    bool byExposure = false;
    double exposureToleranceFraction = 0.05;
};

/**
 * @brief Weighting modes exposed by the standalone stacking UI.
 */
enum class StandaloneStackingWeightingMode
{
    Equal,
    Fwhm,
    Noise,
    Snr,
    Stars,
    Exposure
};

/**
 * @brief One frame of the standalone stacking's list: its header as the engine probed it and the figures the
 * integration reads from it.
 */
struct StandaloneStackingFrame
{
    ImageListProcessHelpers::ImageListEntry entry;

    /// The filter, empty when the header states none.
    QString filter;

    /// When the frame was taken, in UTC: the exposure start, else when the file says it was made, else the file's
    /// modification time.
    QDateTime capturedAtUtc;

    /// The exposure, in seconds; zero when the header states none.
    double exposureSeconds = 0.0;
};

/**
 * @brief Group summary shown in the standalone stacking groups table.
 */
struct StandaloneStackingGroup
{
    QString id;
    QString label;
    QString filter;
    QString session;
    double representativeExposureSeconds = 0.0;
    int frameCount = 0;
    double totalExposureSeconds = 0.0;
    QString outputFileName;
    std::vector<StandaloneStackingFrame> images;
};

/**
 * @brief The weights a group is stacked with: the engine's weighting source and each frame's measurement of the
 * figure it reads, or why the set is stacked with equal weights or not at all.
 */
struct StandaloneStackingWeightingResult
{
    /// The engine's source; `Uniform` for equal weights.
    an::StackWeighting method = an::StackWeighting::Uniform;

    /// One measurement per frame, in the group's order; empty for equal weights, and for noise until the stacking has
    /// measured each frame's pixels and stated their figures through `standaloneNoiseWeights`.
    std::vector<an::StackFrameMeasurement> measurements;

    /// Whether equal weights stand in for the mode asked for.
    bool usedFallback = false;

    /// What a person is told about the weights: why they fell back, or which frame lacks its figure.
    QString warning;

    /// Whether some frames carry the figure and some do not: the engine refuses such a set, naming the first frame
    /// without it, rather than giving those frames zero weight.
    bool partlyMeasured = false;
};

/**
 * @brief Requested transform-role contract for standalone stacking.
 *
 * Standalone stacking does not have a production transform-role consumer yet. This request type exists so future
 * role-aware UI or persistence paths must pass through an explicit guard instead of silently stacking ordinary inputs.
 */
struct StandaloneStackingTransformRoleRequest
{
    QString registrationMetricsDbPath;
    QString stackingMode = QStringLiteral("Stars");
    QString requestedRole;
    bool drizzle = false;
    std::vector<FrameId> frameIds;
};

/**
 * @brief Result of validating a standalone transform-role stacking request.
 */
struct StandaloneStackingTransformRoleResult
{
    bool allowed = false;
    QString effectiveRole;
    QString error;
    std::vector<QString> resolvedInputPaths;
};

/// The session a frame belongs to: the UTC date it was taken on, else the date its file was last changed, else its
/// folder's name.
[[nodiscard]] QString standaloneStackingSessionLabel(const QDateTime& capturedAtUtc, const QString& filePath);

[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<StandaloneStackingGroup>
buildStandaloneStackingGroups(const std::vector<StandaloneStackingFrame>& images,
                              const StandaloneStackingGroupingOptions& options, const QString& outputTemplate);

[[nodiscard]] AFTERNIGHTPROCESSES_API QString resolveStandaloneStackingOutputName(const QString& outputTemplate,
                                                                                  const StandaloneStackingGroup& group);

[[nodiscard]] AFTERNIGHTPROCESSES_API bool
standaloneStackingWeightingRequiresMetrics(StandaloneStackingWeightingMode mode) noexcept;

[[nodiscard]] StandaloneStackingWeightingMode standaloneStackingWeightingModeFromText(const QString& text) noexcept;

[[nodiscard]] QString standaloneStackingWeightingLabel(StandaloneStackingWeightingMode mode);

[[nodiscard]] AFTERNIGHTPROCESSES_API StandaloneStackingTransformRoleResult
validateStandaloneStackingTransformRoleSelection(const StandaloneStackingTransformRoleRequest& request);

/// The engine's weighting source a mode reads: equal weights `Uniform`, FWHM `Fwhm`, Stars `StarCount`, Noise
/// `Noise`, SNR `SignalToNoise`, Exposure `Exposure`.
[[nodiscard]] an::StackWeighting standaloneStackingWeightingSource(StandaloneStackingWeightingMode mode) noexcept;

/// The weights a group is stacked with: the exposure each frame's header states, or the width, star count, roundness
/// and signal to noise the newest registration of each frame recorded in the registration database. Noise is
/// measured by the stacking on each frame's pixels and stated through `standaloneNoiseWeights`, so no measurement is
/// made here.
///
/// @param registrationMetricsDbPath the database the registration wrote beside its outputs.
/// @param images the group's frames.
/// @param mode the mode asked for.
/// @return the source, the measurements and what a person is told.
[[nodiscard]] AFTERNIGHTPROCESSES_API StandaloneStackingWeightingResult
planStandaloneStackingWeights(const QString& registrationMetricsDbPath,
                              const std::vector<StandaloneStackingFrame>& images, StandaloneStackingWeightingMode mode);

/// The weights a Noise-weighted group is stacked with, from each frame's background noise as the stacking measured it
/// on the frame's pixels: the engine's Noise source over that figure alone - each frame weighed by one over its square.
/// Equal weights, said, when no frame has a figure; a set only some of whose frames have one is said as the other modes
/// say it, for the stack to refuse.
///
/// @param images the group's frames.
/// @param backgroundNoise each frame's background noise, in the group's order, or nothing for a frame whose noise
///        could not be measured; a figure that is not finite and positive counts as nothing.
/// @return the source, one measurement per frame and what a person is told.
[[nodiscard]] AFTERNIGHTPROCESSES_API StandaloneStackingWeightingResult standaloneNoiseWeights(
    const std::vector<StandaloneStackingFrame>& images, const std::vector<std::optional<double>>& backgroundNoise);
} // namespace an::ui
