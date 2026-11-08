// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A lights group's stacking settings read through the closed choice maps, what the stack lets go of, and the exposure
/// merge's buckets and blend.

#include "stdafx.hpp"
#include "StackingParamsUtils.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>

namespace
{

[[nodiscard]] QString normalizedRgbFilterToken(const QString& filter)
{
    return filter.trimmed().toCaseFolded();
}

[[nodiscard]] bool containsRgbToken(const QString& filter, const QString& token)
{
    return normalizedRgbFilterToken(filter).contains(token.toCaseFolded());
}

struct RgbCandidateBucket
{
    std::vector<ImageIntegration::RgbIntegrationGroupCandidate> red;
    std::vector<ImageIntegration::RgbIntegrationGroupCandidate> green;
    std::vector<ImageIntegration::RgbIntegrationGroupCandidate> blue;
};

[[nodiscard]] an::Status unknownText(const char* setting, const QString& text)
{
    return an::Status::failure(an::ErrorCode::InvalidArgument,
                               std::string("Unknown ") + setting + " '" + text.trimmed().toUtf8().toStdString() + "'");
}

[[nodiscard]] bool isNone(const QString& text)
{
    return text.trimmed().isEmpty() || text.trimmed().compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] QString rejectionLabel(const an::StackRejection rejection)
{
    switch (rejection)
    {
    case an::StackRejection::None:
        return QStringLiteral("no rejection");
    case an::StackRejection::SigmaClipping:
        return QStringLiteral("Sigma Clipping");
    case an::StackRejection::MadSigmaClipping:
        return QStringLiteral("MAD Sigma Clipping");
    case an::StackRejection::MedianSigmaReplacement:
        return QStringLiteral("Median Sigma");
    case an::StackRejection::WinsorizedSigmaClipping:
        return QStringLiteral("Winsorized Sigma Clipping");
    case an::StackRejection::LinearFitClipping:
        return QStringLiteral("Linear Fit Clipping");
    case an::StackRejection::PercentileClipping:
        return QStringLiteral("Percentile Clipping");
    case an::StackRejection::GeneralizedEsdTest:
        return QStringLiteral("Generalized ESD Test");
    }
    return QStringLiteral("an unknown rejection");
}

[[nodiscard]] QString combinationLabel(const an::StackCombination combination)
{
    switch (combination)
    {
    case an::StackCombination::Average:
        return QStringLiteral("Average");
    case an::StackCombination::Sum:
        return QStringLiteral("Sum");
    case an::StackCombination::Median:
        return QStringLiteral("Median");
    case an::StackCombination::Minimum:
        return QStringLiteral("Minimum");
    case an::StackCombination::Maximum:
        return QStringLiteral("Maximum");
    case an::StackCombination::AutoAdaptiveWeightedAverage:
        return QStringLiteral("Auto-Adaptive Weighted Average");
    }
    return QStringLiteral("an unknown combination");
}

} // namespace

namespace ImageIntegration
{
bool filterMatchesRgbChannel(const QString& filter, RgbCombinationChannel channel)
{
    switch (channel)
    {
    case RgbCombinationChannel::Red:
        return containsRgbToken(filter, QStringLiteral("red"));
    case RgbCombinationChannel::Green:
        return containsRgbToken(filter, QStringLiteral("green"));
    case RgbCombinationChannel::Blue:
        return containsRgbToken(filter, QStringLiteral("blue"));
    }

    return false;
}

std::vector<RgbIntegrationGroupSet>
findEligibleRgbIntegrationGroups(const std::vector<RgbIntegrationGroupCandidate>& candidates)
{
    QHash<QString, RgbCandidateBucket> by_registration_group;
    for (const auto& candidate : candidates)
    {
        const QString registration_key = candidate.registrationGroupKey.trimmed();
        if (registration_key.isEmpty())
        {
            continue;
        }

        RgbCandidateBucket& bucket = by_registration_group[registration_key];
        if (filterMatchesRgbChannel(candidate.filter, RgbCombinationChannel::Red))
        {
            bucket.red.push_back(candidate);
        }
        if (filterMatchesRgbChannel(candidate.filter, RgbCombinationChannel::Green))
        {
            bucket.green.push_back(candidate);
        }
        if (filterMatchesRgbChannel(candidate.filter, RgbCombinationChannel::Blue))
        {
            bucket.blue.push_back(candidate);
        }
    }

    std::vector<RgbIntegrationGroupSet> groups;
    groups.reserve(static_cast<size_t>(by_registration_group.size()));
    for (auto it = by_registration_group.constBegin(); it != by_registration_group.constEnd(); ++it)
    {
        const RgbCandidateBucket& bucket = it.value();
        if (bucket.red.size() != 1u || bucket.green.size() != 1u || bucket.blue.size() != 1u)
        {
            continue;
        }

        groups.push_back(
            RgbIntegrationGroupSet{it.key(), bucket.red.front(), bucket.green.front(), bucket.blue.front()});
    }

    std::sort(groups.begin(), groups.end(), [](const RgbIntegrationGroupSet& lhs, const RgbIntegrationGroupSet& rhs) {
        return lhs.registrationGroupKey < rhs.registrationGroupKey;
    });
    return groups;
}

bool hasEligibleRgbIntegrationGroup(const std::vector<RgbIntegrationGroupCandidate>& candidates)
{
    return !findEligibleRgbIntegrationGroups(candidates).empty();
}

QString combinedRgbGroupKey(const QString& registrationGroupKey)
{
    const QString key = registrationGroupKey.trimmed();
    return key.isEmpty() ? QStringLiteral("rgb") : QStringLiteral("rgb|%1").arg(key);
}

QString combinedRgbOutputFilename(const QString& registrationGroupKey)
{
    const QString key = registrationGroupKey.trimmed();
    const QString label = key.isEmpty() ? QStringLiteral("RGB") : QStringLiteral("RGB_%1").arg(key);
    return QStringLiteral("stacked_%1.fit").arg(ImageIntegration::sanitizeFilename(label));
}

an::Result<IntegrationStackChoice> integrationStackChoice(const StackingSettings& settings)
{
    IntegrationStackChoice choice;
    const PersistedChoiceValue method = persistedChoiceFromText(PersistedChoice::StackMethod, settings.stackMethod);
    if (!method.known)
    {
        return unknownText("stacking method", settings.stackMethod);
    }
    if (const std::optional<an::StackCombination> combination = stackCombinationForKey(method.key))
    {
        choice.combination = *combination;
        if (choice.combination == an::StackCombination::Average)
        {
            const PersistedChoiceValue rejection =
                persistedChoiceFromText(PersistedChoice::RejectionMethod, settings.rejectionMethod);
            if (!rejection.known)
            {
                return unknownText("rejection method", settings.rejectionMethod);
            }
            choice.rejection = stackRejectionForKey(rejection.key).value_or(an::StackRejection::None);
        }
    }
    else if (const std::optional<an::StackRejection> rejection = stackRejectionForKey(method.key))
    {
        choice.rejection = *rejection;
    }
    else
    {
        return unknownText("stacking method", settings.stackMethod);
    }

    const PersistedChoiceValue weighting = persistedChoiceFromText(PersistedChoice::Weighting, settings.weighting);
    if (!weighting.known)
    {
        return unknownText("weighting", settings.weighting);
    }
    choice.weighting = stackWeightingForKey(weighting.key).value_or(an::StackWeighting::Uniform);

    const PersistedChoiceValue scaling =
        persistedChoiceFromText(PersistedChoice::StarCountScaling, settings.starCountScaling);
    if (!scaling.known)
    {
        return unknownText("star count scaling", settings.starCountScaling);
    }
    if (scaling.key == QLatin1String("linear"))
    {
        choice.starCountScaling = an::StarCountScaling::Linear;
    }
    else if (scaling.key == QLatin1String("exponential"))
    {
        choice.starCountScaling = an::StarCountScaling::Exponential;
    }
    else if (scaling.key == QLatin1String("roundness-weighted"))
    {
        choice.starCountScaling = an::StarCountScaling::RoundnessWeighted;
    }
    else
    {
        choice.starCountScaling = an::StarCountScaling::Quadratic;
    }
    return choice;
}

bool combinationTakesWeights(const an::StackCombination combination) noexcept
{
    return combination == an::StackCombination::Average;
}

QString describeStackChoice(const IntegrationStackChoice& choice)
{
    if (choice.combination == an::StackCombination::Average && choice.rejection != an::StackRejection::None)
    {
        return QStringLiteral("Average with %1").arg(rejectionLabel(choice.rejection));
    }
    return combinationLabel(choice.combination);
}

StackingSettings sanitizeStackingSettings(StackingSettings settings)
{
    settings.cometStackingMode = normalizeCometStackingMode(settings.cometStackingMode);
    if (settings.restackExcludingFlaggedFrames)
    {
        settings.spatialRejectionClustering = true;
    }

    const an::Result<IntegrationStackChoice> choice = integrationStackChoice(settings);
    // An unknown text is left for the stack to refuse by name; nothing is let go of on its account.
    const bool weighted = !choice.hasValue() || combinationTakesWeights(choice->combination);
    const bool rejects = !choice.hasValue() || choice->rejection != an::StackRejection::None;

    if (!weighted)
    {
        settings.weighting = QStringLiteral("None");
        settings.localWeighting.enabled = false;
    }
    else if (isNone(settings.weighting))
    {
        settings.localWeighting.enabled = false;
    }

    if (!rejects)
    {
        if (choice.hasValue() && choice->combination != an::StackCombination::Average)
        {
            settings.rejectionMethod = QStringLiteral("None");
        }
        settings.createRejectionMaps = false;
        settings.mergeRejectionMaps = false;
        settings.spatialRejectionClustering = false;
        settings.restackExcludingFlaggedFrames = false;
    }
    else if (!settings.createRejectionMaps)
    {
        settings.mergeRejectionMaps = false;
    }

    if (settings.hdr.enabled)
    {
        settings.normalizationMode = NormalizationMode::None;
        settings.localNormalization.enabled = false;
        settings.manualNormalizationReference.clear();
        settings.createRejectionMaps = false;
        settings.mergeRejectionMaps = false;
        settings.createQualityMaps = false;
        settings.drizzle.enabled = false;
        settings.weighting = QStringLiteral("None");
        settings.localWeighting.enabled = false;
        settings.spatialRejectionClustering = false;
        settings.restackExcludingFlaggedFrames = false;
    }

    if (!settings.spatialRejectionClustering)
    {
        settings.restackExcludingFlaggedFrames = false;
    }

    if (settings.drizzle.enabled)
    {
        settings.restackExcludingFlaggedFrames = false;
    }

    if (usesCometStackingMode(settings.cometStackingMode))
    {
        settings.weighting = QStringLiteral("None");
        settings.localWeighting.enabled = false;
    }

    return settings;
}

namespace
{

[[nodiscard]] QString combinationTakesNoWeightsNote(const QString& method, const QString& weighting)
{
    return QStringLiteral("%1 takes no frame weights; the %2 weighting is not applied.").arg(method, weighting);
}

[[nodiscard]] QString cometWeighsEquallyNote(const QString& weighting)
{
    return QStringLiteral("A comet stack weighs its frames equally; the %1 weighting is not applied.").arg(weighting);
}

} // namespace

QStringList stackingSettingsNotes(const StackingSettings& settings)
{
    QStringList notes;
    const an::Result<IntegrationStackChoice> choice = integrationStackChoice(settings);
    if (!choice.hasValue())
    {
        return notes;
    }
    const QString method = describeStackChoice(*choice);
    if (!combinationTakesWeights(choice->combination) && !isNone(settings.weighting))
    {
        notes.append(combinationTakesNoWeightsNote(method, settings.weighting.trimmed()));
    }
    if (choice->combination != an::StackCombination::Average && !isNone(settings.rejectionMethod) &&
        settings.stackMethod.trimmed().compare(QStringLiteral("Average"), Qt::CaseInsensitive) != 0)
    {
        const PersistedChoiceValue stored = persistedChoiceFromText(PersistedChoice::StackMethod, settings.stackMethod);
        if (stored.known && stackCombinationForKey(stored.key).has_value())
        {
            notes.append(QStringLiteral("%1 rejects nothing; the %2 rejection is not applied.")
                             .arg(method, settings.rejectionMethod.trimmed()));
        }
    }
    if (choice->rejection == an::StackRejection::None &&
        (settings.createRejectionMaps || settings.spatialRejectionClustering || settings.restackExcludingFlaggedFrames))
    {
        notes.append(
            QStringLiteral("%1 rejects nothing; no rejection map or spatial rejection analysis is made.").arg(method));
    }
    if (settings.hdr.enabled)
    {
        const QString weighting = isNone(settings.weighting)
                                      ? QStringLiteral("frame weighting")
                                      : QStringLiteral("the %1 weighting").arg(settings.weighting.trimmed());
        notes.append(QStringLiteral("The exposure merge stacks each exposure on its own: normalization, %1, rejection "
                                    "maps, quality maps, drizzle, local weighting and spatial rejection analysis are "
                                    "not applied.")
                         .arg(weighting));
    }
    else if (settings.drizzle.enabled && settings.restackExcludingFlaggedFrames)
    {
        notes.append(QStringLiteral("A drizzled stack is not stacked again without the frames its rejection flags."));
    }
    if (usesCometStackingMode(normalizeCometStackingMode(settings.cometStackingMode)) && !isNone(settings.weighting))
    {
        notes.append(cometWeighsEquallyNote(settings.weighting.trimmed()));
    }
    if (sanitizeStackingSettings(settings).localWeighting.enabled)
    {
        const LocalWeightingSettings& stored = settings.localWeighting;
        const an::LocalWeightingRequest request = localWeightingRequestFor(stored);
        QStringList adjusted;
        if (static_cast<qint64>(request.windowSize) != stored.windowSize)
        {
            adjusted << QStringLiteral("window %1 as %2").arg(stored.windowSize).arg(request.windowSize);
        }
        if (static_cast<qint64>(request.gridSpacing) != stored.gridSpacing)
        {
            adjusted << QStringLiteral("grid spacing %1 as %2").arg(stored.gridSpacing).arg(request.gridSpacing);
        }
        if (!(request.snrExponent == stored.snrWeightExponent))
        {
            adjusted << QStringLiteral("exponent %1 as %2")
                            .arg(stored.snrWeightExponent, 0, 'f', 2)
                            .arg(request.snrExponent, 0, 'f', 2);
        }
        if (!adjusted.isEmpty())
        {
            notes.append(QStringLiteral("Per-pixel weighting settings the stack refuses run at the nearest value it "
                                        "takes: %1.")
                             .arg(adjusted.join(QStringLiteral(", "))));
        }
    }
    return notes;
}

QString weightingNotAppliedNote(const StackingSettings& settings)
{
    if (isNone(settings.weighting))
    {
        return {};
    }
    const QString weighting = settings.weighting.trimmed();
    if (settings.hdr.enabled)
    {
        return QStringLiteral("The exposure merge stacks each exposure on its own; the %1 weighting is not applied.")
            .arg(weighting);
    }
    if (usesCometStackingMode(normalizeCometStackingMode(settings.cometStackingMode)))
    {
        return cometWeighsEquallyNote(weighting);
    }
    const an::Result<IntegrationStackChoice> choice = integrationStackChoice(settings);
    if (choice.hasValue() && !combinationTakesWeights(choice->combination))
    {
        return combinationTakesNoWeightsNote(describeStackChoice(*choice), weighting);
    }
    return {};
}

an::LocalWeightingRequest localWeightingRequestFor(const LocalWeightingSettings& settings)
{
    an::LocalWeightingRequest request;
    request.enabled = settings.enabled;
    request.windowSize =
        static_cast<std::uint32_t>((std::max)(settings.windowSize, static_cast<int>(an::kMinimumLocalWeightWindow)));
    if (request.windowSize % 2u == 0u)
    {
        ++request.windowSize;
    }
    request.gridSpacing = static_cast<std::uint32_t>(
        (std::max)(settings.gridSpacing, static_cast<int>(an::kMinimumLocalWeightGridSpacing)));
    if (!std::isfinite(settings.snrWeightExponent))
    {
        request.snrExponent = an::kDefaultLocalWeightSnrExponent;
    }
    else
    {
        request.snrExponent =
            settings.snrWeightExponent > 0.0 ? settings.snrWeightExponent : kMinimumLocalWeightingSnrExponent;
    }
    return request;
}

std::uint32_t combinedFrameCount(const std::span<const an::CodecCard> cards)
{
    for (const char* keyword : {"STACKCNT", "NCOMBINE"})
    {
        const auto card = std::find_if(cards.begin(), cards.end(), [keyword](const an::CodecCard& candidate) {
            return candidate.origin() == an::CodecNamespace::Fits && candidate.keyword() == keyword;
        });
        if (card == cards.end())
        {
            continue;
        }
        QString text = QString::fromStdString(card->value()).trimmed();
        if (text.size() >= 2 && text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\'')))
        {
            text = text.mid(1, text.size() - 2).trimmed();
        }
        bool read = false;
        const double count = text.toDouble(&read);
        if (read && std::isfinite(count) && count >= 1.0)
        {
            constexpr double kMostFrames = static_cast<double>((std::numeric_limits<std::uint32_t>::max)());
            return static_cast<std::uint32_t>((std::min)(std::floor(count), kMostFrames));
        }
    }
    return 1u;
}

StackingQualityMapProductPolicy stackingQualityMapProductPolicy(const StackingSettings& sanitized)
{
    if (!sanitized.createQualityMaps || sanitized.hdr.enabled)
    {
        return {};
    }
    const an::Result<IntegrationStackChoice> choice = integrationStackChoice(sanitized);
    const bool weighted = choice.hasValue() && combinationTakesWeights(choice->combination) &&
                          (choice->weighting != an::StackWeighting::Uniform || sanitized.localWeighting.enabled);
    return {true, true, true, weighted};
}

QString cometStackingTemporarilyDisabledMessage(const QString& mode)
{
    return QStringLiteral("%1 stacking requires an active comet registration run with exact role-qualified products.")
        .arg(normalizeCometStackingMode(mode));
}

an::Result<std::vector<ExposureBucket>> exposureMergeBuckets(const std::vector<double>& exposures)
{
    for (std::size_t index = 0; index < exposures.size(); ++index)
    {
        if (!std::isfinite(exposures[index]) || exposures[index] <= 0.0)
        {
            return an::Status::failure(an::ErrorCode::InvalidArgument,
                                       "The exposure merge needs a positive exposure for every frame; frame " +
                                           std::to_string(index + 1u) + " states none");
        }
    }
    std::vector<std::uint32_t> order(exposures.size());
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](const std::uint32_t left, const std::uint32_t right) {
        return exposures[left] < exposures[right];
    });

    std::vector<ExposureBucket> buckets;
    for (const std::uint32_t frame : order)
    {
        const double exposure = exposures[frame];
        if (!buckets.empty())
        {
            ExposureBucket& current = buckets.back();
            const double relative =
                std::abs(exposure - current.exposureSeconds) / (std::max)(current.exposureSeconds, 1e-9);
            if (relative <= kExposureMergeGroupingTolerance)
            {
                current.frames.push_back(frame);
                const double count = static_cast<double>(current.frames.size());
                current.exposureSeconds = (current.exposureSeconds * (count - 1.0) + exposure) / count;
                continue;
            }
        }
        buckets.push_back(ExposureBucket{.exposureSeconds = exposure, .frames = {frame}});
    }
    if (buckets.size() < 2u)
    {
        return an::Status::failure(an::ErrorCode::EmptySelection,
                                   "The exposure merge needs at least two distinct exposures");
    }
    std::stable_sort(buckets.begin(), buckets.end(), [](const ExposureBucket& left, const ExposureBucket& right) {
        return left.exposureSeconds > right.exposureSeconds;
    });
    return buckets;
}

double exposureMergeBlendWeight(const double value) noexcept
{
    if (!(value > kExposureMergeHighlightLow))
    {
        return 0.0;
    }
    if (value >= kExposureMergeHighlightHigh)
    {
        return 1.0;
    }
    const double t = (value - kExposureMergeHighlightLow) / (kExposureMergeHighlightHigh - kExposureMergeHighlightLow);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace ImageIntegration
