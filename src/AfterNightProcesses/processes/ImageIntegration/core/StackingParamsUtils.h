// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What a lights group's stacking settings name, read through the closed choice maps, and what the stack lets go of.
///
/// **A text the maps do not know is refused, not guessed.** Reading an unknown stacking method as an average and an
/// unknown rejection as none would run a method nobody chose while the report named the one that was stored. The
/// method, the rejection, the weighting and the star-count scaling are read through `PersistedChoiceMaps.h`, and a text
/// none of them names stops the group, naming the text.
///
/// **What the stack does not apply is said.** A combination that takes no rejection or no weights, a rejection the
/// frames are too few for, and the settings an exposure merge replaces are let go of by `sanitizeStackingSettings`,
/// a per-pixel weighting the engine would refuse runs at the nearest values it takes, and `stackingSettingsNotes`
/// states each one, so the run's log and warnings name the method that ran rather than the one stored.

#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ImageIntegration
{
enum class RgbCombinationChannel
{
    Red,
    Green,
    Blue
};

struct RgbIntegrationGroupCandidate
{
    QString groupKey;
    QString registrationGroupKey;
    QString filter;
    QString outputFilename;
    int lightFrameCount = 0;
};

struct RgbIntegrationGroupSet
{
    QString registrationGroupKey;
    RgbIntegrationGroupCandidate red;
    RgbIntegrationGroupCandidate green;
    RgbIntegrationGroupCandidate blue;
};

[[nodiscard]] bool filterMatchesRgbChannel(const QString& filter, RgbCombinationChannel channel);
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<RgbIntegrationGroupSet>
findEligibleRgbIntegrationGroups(const std::vector<RgbIntegrationGroupCandidate>& candidates);
[[nodiscard]] AFTERNIGHTPROCESSES_API bool
hasEligibleRgbIntegrationGroup(const std::vector<RgbIntegrationGroupCandidate>& candidates);
[[nodiscard]] AFTERNIGHTPROCESSES_API QString combinedRgbGroupKey(const QString& registrationGroupKey);
[[nodiscard]] AFTERNIGHTPROCESSES_API QString combinedRgbOutputFilename(const QString& registrationGroupKey);

/// What a group's stacking settings name.
struct IntegrationStackChoice
{
    /// How the surviving samples of a pixel become its value.
    an::StackCombination combination = an::StackCombination::Average;

    /// Which samples survive: a rejection beside an average, or a stored method that names one.
    an::StackRejection rejection = an::StackRejection::None;

    /// Where the frame weights come from; `Uniform` for none.
    an::StackWeighting weighting = an::StackWeighting::Uniform;

    /// How star counts become weights.
    an::StarCountScaling starCountScaling = an::StarCountScaling::Quadratic;
};

/// Reads a group's settings through the closed choice maps.
///
/// @param settings the settings, as stored or sanitized.
/// @return the choice; `ErrorCode::InvalidArgument` naming the setting and its text when the method, the rejection,
///         the weighting or the star-count scaling is a text the maps do not know.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<IntegrationStackChoice>
integrationStackChoice(const StackingSettings& settings);

/// Whether a combination takes per-frame weights: the average, with or without a rejection.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool combinationTakesWeights(an::StackCombination combination) noexcept;

/// The method a choice runs, as a person reads it: "Average", "Median", "Average with Winsorized Sigma Clipping".
[[nodiscard]] QString describeStackChoice(const IntegrationStackChoice& choice);

/// The settings the stack uses: weighting let go of beside a combination that takes none, rejection and its maps and
/// spatial analysis beside one that rejects nothing, the merge beside no maps, the restack beside no spatial analysis
/// or a drizzle, and everything an exposure merge replaces - the frame weighting among it, since each exposure is
/// stacked on its own. An unknown text is left as it is for `integrationStackChoice` to refuse.
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingSettings sanitizeStackingSettings(StackingSettings settings);

/// What `sanitizeStackingSettings` lets go of in a group's settings, and a per-pixel weighting the stack runs at
/// values other than the stored ones (`localWeightingRequestFor`), one sentence each, naming the setting.
///
/// @param settings the settings as stored.
/// @return the sentences; empty when nothing is let go of or changed.
[[nodiscard]] AFTERNIGHTPROCESSES_API QStringList stackingSettingsNotes(const StackingSettings& settings);

/// Why the stack applies no frame weighting although the settings store one - the exposure merge, a comet stack or a
/// combination that takes no weights - as one sentence naming the weighting, worded as `stackingSettingsNotes` words
/// it.
///
/// @param settings the settings as stored.
/// @return the sentence; empty when the stored weighting is applied or none is stored.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString weightingNotAppliedNote(const StackingSettings& settings);

/// The smallest per-pixel weighting exponent the dialog offers, which a stored exponent that is not positive runs at.
inline constexpr double kMinimumLocalWeightingSnrExponent = 0.1;

/// The per-pixel weighting the stack runs for a group's settings, at the nearest values the engine takes: the window
/// raised to `an::kMinimumLocalWeightWindow` and made odd, the grid spacing raised to
/// `an::kMinimumLocalWeightGridSpacing`, and an exponent that is not positive raised to
/// `kMinimumLocalWeightingSnrExponent` - one that is not a number at all runs at the engine's default. The dialog's
/// ranges leave every value as it is; only a hand-edited or earlier session reaches a value the engine refuses.
///
/// @param settings the stored per-pixel weighting.
/// @return the request, enabled as the settings are.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::LocalWeightingRequest
localWeightingRequestFor(const LocalWeightingSettings& settings);

/// How many frames an image combines, as its header states it: `STACKCNT`, which a master the lights stack writes
/// carries - or else the `NCOMBINE` other programs write; one when it states neither, or no count of at least one.
///
/// @param cards the image's header cards.
/// @return the count's whole frames, at least one.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::uint32_t combinedFrameCount(std::span<const an::CodecCard> cards);

/// Which quality maps a stack writes beside its master: none unless they were asked for, and none beside an exposure
/// merge; the coverage, the variance and the signal-to-noise gain, and the weight sum where a weighting applies.
struct StackingQualityMapProductPolicy
{
    bool coverage = false;
    bool variance = false;
    bool snrGain = false;
    bool weightSum = false;

    [[nodiscard]] bool requestsAnyProduct() const noexcept
    {
        return coverage || variance || snrGain || weightSum;
    }
};

/// The quality maps a group's sanitized settings ask for.
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingQualityMapProductPolicy
stackingQualityMapProductPolicy(const StackingSettings& sanitized);

[[nodiscard]] QString cometStackingTemporarilyDisabledMessage(const QString& mode);

/// The tolerance two exposures are merged into one bucket of an exposure merge within, as a fraction of the bucket's.
inline constexpr double kExposureMergeGroupingTolerance = 0.05;

/// The fraction of full scale at and below which a longer exposure is kept whole in an exposure merge.
inline constexpr double kExposureMergeHighlightLow = 0.75;

/// The fraction of full scale at and above which a shorter exposure replaces the longer one whole.
inline constexpr double kExposureMergeHighlightHigh = 0.95;

/// The frames of one exposure in an exposure merge.
struct ExposureBucket
{
    /// The mean exposure of the frames, in seconds.
    double exposureSeconds = 0.0;

    /// The frames, as indices into the group's.
    std::vector<std::uint32_t> frames;
};

/// Sorts a merge's frames into buckets of one exposure: in ascending order, a frame within the tolerance of the
/// current bucket's mean joining it, then the buckets longest first.
///
/// @param exposures each frame's exposure in seconds.
/// @return the buckets, longest first; `ErrorCode::InvalidArgument` naming the frame when an exposure is not finite
///         and positive; `ErrorCode::EmptySelection` when the frames make fewer than two buckets.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::vector<ExposureBucket>>
exposureMergeBuckets(const std::vector<double>& exposures);

/// The weight a shorter exposure takes over the merge so far at a sample of `value`: zero at and below the highlight
/// low, one at and above the highlight high, and the smooth step between.
[[nodiscard]] AFTERNIGHTPROCESSES_API double exposureMergeBlendWeight(double value) noexcept;

} // namespace ImageIntegration
