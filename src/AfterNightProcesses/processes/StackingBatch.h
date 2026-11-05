// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Stacking's run over the engine: each group of files stacked by one `executeStack` call over a source that
/// decodes each frame as the stack asks for it, the window's settings stated as the engine's stack request, the
/// result demosaiced when asked, written whole with the exposure the window asks for, and its rejection maps beside it.
///
/// **The settings are the integration's records.** The window states a `StackingSettings` and a
/// `LocalNormalizationSettings`, as the integration does; `standaloneStackRequest` reads them through the same closed
/// maps the integration's persistence reads (`ImageIntegration::PersistedChoiceMaps.h`), so a text neither knows is
/// refused rather than guessed. Settings a combination cannot use are let go before the request is made: weighting
/// with no weighted combination, rejection beside anything but an average.
///
/// **Local normalization is fitted by the stack against its reference frame.** The engine's stack fits the
/// local-normalization document itself, with the grid, interpolation, background estimator, percentile, clipping,
/// smoothing, scale mode and scale smoothing the window states; a reference built by integrating the best frames, the
/// evaluation criteria and the PSF scale evaluation belong to the integration's normalization stage, and the
/// standalone stack does not apply them.
///
/// **An exact zero and a sample that is not a number mean nothing in every frame.** Each frame is read under
/// `ValidityRule::ZeroIsNoData`, a floating sample that is not a number read as an exact zero, so the zero border an
/// alignment leaves where a frame does not reach, and what a calibration left where its flat was zero, are neither
/// combined nor judged by a rejection, and an element no frame reached is the stack's no-data marker.
///
/// **Rejection maps are fractions of the group's frames.** The engine counts the samples rejected at each element;
/// each map is written as Float32 fractions of the group's frames, the merged map holding the sum at one.
///
/// **Noise weighting measures each frame's pixels.** A Noise-weighted average weighs each frame by one over the square
/// of its background noise, which the run measures on the frame's samples, in an operation of its own before the
/// stack's; a frame of no measurable noise is said as the other weightings say a frame without its figure.
///
/// **What the engine says of a stack is kept.** Its warnings about a group's stack are logged as they are said and
/// returned with the group, for the window to show once the run has ended.
#pragma once

#include "AfterNightProcessesExport.h"

#include "app/DemosaicTextMaps.h"
#include "processes/core/StandaloneStackingUtils.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui::processes
{

/// The name the registration database has beside the stacked outputs, which the weights and comet products are read
/// from.
inline constexpr const char* kRegistrationMetricsDatabaseName = "registration_metrics.db";

/// Lets go of the settings a stack's combination cannot use: weighting beside a combination that takes no weights,
/// rejection and its maps beside anything but an average, the merge beside no maps, and weighting in a comet mode.
///
/// @param settings the settings.
/// @return the settings the stack uses.
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingSettings sanitizeStandaloneStackingSettings(StackingSettings settings);

/// The engine's local-normalization request for the integration's settings: the grid size (held to the engine's
/// range), interpolation, background estimator, percentile, sigma clip and its iterations, background smoothing,
/// clipping range, scale mode and scale smoothing.
///
/// @param settings the settings.
/// @return the request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::LocalNormalizationRequest
standaloneLocalNormalizationRequest(const LocalNormalizationSettings& settings);

/// The engine's stack request for a group: the combination and rejection the method and rejection texts name, the
/// thresholds by the rejection's kind - sigma or linear-fit thresholds, the percentile clipping's fractions of the
/// median, the ESD test's outlier fraction and significance - the weighting source, the normalization scope, method and
/// estimator, the output representation and the rejection maps.
///
/// @param settings the sanitized settings.
/// @param input what every frame is.
/// @param frameCount how many frames.
/// @param weighting the weighting source the group's measurements serve.
/// @param output64 whether the stack is written as Float64.
/// @return the request; `ErrorCode::InvalidArgument` for a method, rejection, normalization or estimator text the
///         closed maps do not know; or what the engine's stack model refuses, named.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::StackRequest>
standaloneStackRequest(const StackingSettings& settings, const an::ImageDescriptor& input, std::uint32_t frameCount,
                       an::StackWeighting weighting, bool output64);

/// The exposure a stacked image states: the reference frame's, or the sum of every frame's, a frame that states none
/// counted at the reference's exposure when it states one.
///
/// @param exposures each frame's exposure in seconds, in the group's order, or nothing for a frame that states none.
/// @param reference the reference frame.
/// @param sum whether the exposures are summed.
/// @return the exposure, or nothing when no frame states one.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<double>
standaloneStackExposure(const std::vector<std::optional<double>>& exposures, std::size_t reference, bool sum);

/// The file a rejection map of a stacked output is written to: beside it, `_rej_l`, `_rej_h` or the merged `_rej_l+h`
/// after its base name, with its extension.
///
/// @param outputPath the stacked output.
/// @param suffix "rej_l", "rej_h" or "rej_l+h".
/// @return the path.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString standaloneRejectionMapPath(const QString& outputPath,
                                                                         const QString& suffix);

/// What one stacking run asks for, read from the window on the GUI thread.
struct StandaloneStackRun
{
    /// The stacking settings, sanitized.
    StackingSettings stacking;

    /// Whether the stack is written as Float64.
    bool output64 = false;

    /// Whether the written exposure is the sum of the frames'.
    bool sumExposureTimes = false;

    /// Whether a mosaic result is demosaiced before it is written.
    bool debayer = false;

    /// The filter arrangement the demosaic reads: a stated one, or the one the image states.
    an::ui::BayerPatternChoice cfaPattern;

    /// The demosaic method.
    an::DemosaicMethod demosaicMethod = an::DemosaicMethod::Rcd;

    /// The directory the outputs are written to and the registration database is read from.
    QString outputDirectory;
};

/// One file a group's stack wrote.
struct StackedFile
{
    /// The file.
    QString path;

    /// What it is, as its view's history names it.
    QString description;

    /// The image written, a new asset of the session, for a view to show.
    an::consumer::ImageAsset asset;
};

/// What stacking one group did.
struct StackedGroup
{
    /// The stacked image first, then its rejection maps.
    std::vector<StackedFile> files;

    /// How many frames were stacked.
    std::uint32_t frames = 0u;

    /// What a person is told about the weights, or empty.
    QString weightingWarning;

    /// What the engine warned of the group's stack, each a sentence, in the order it was said; each was logged as it
    /// was said.
    QStringList warnings;
};

/// Stacks one group: the frames resolved for the run's comet mode, their weights planned from the registration
/// database, the stack executed in one admitted operation, the result demosaiced when asked, and every file written
/// whole.
///
/// @param session the process's session.
/// @param group the group.
/// @param run the run's settings.
/// @param stop stops the stack.
/// @param progress receives the stack's progress as a fraction, on the worker; may be empty.
/// @return what was written; `ErrorCode::Cancelled` when stopped; or what a step refused, naming the group.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<StackedGroup>
stackStandaloneGroup(const an::consumer::EngineSession& session, const StandaloneStackingGroup& group,
                     const StandaloneStackRun& run, std::stop_token stop, std::function<void(double)> progress);

} // namespace an::ui::processes
