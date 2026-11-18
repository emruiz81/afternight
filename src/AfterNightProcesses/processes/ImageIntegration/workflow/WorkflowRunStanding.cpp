// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/ImageIntegration/workflow/WorkflowRunStanding.h"

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowTimeEstimator.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ImageIntegration
{

namespace
{

/// The least a run has to have taken, and the least of its way it has to have come, before the time it has left is
/// told: before them the figure swings with every report.
constexpr qint64 kRemainingLeastElapsedMs = 10'000;
constexpr double kRemainingLeastFraction = 0.05;

/// The least share of a run a stage weighs: a stage the manifest asks nothing countable of still takes its turn.
constexpr double kLeastStageShare = 0.01;

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate("ImageIntegration::WorkflowRunStanding", text);
}

} // namespace

QString workflowStageTitle(const WorkflowStage stage)
{
    switch (stage)
    {
    case WorkflowStage::Bias:
        return translated("Bias");
    case WorkflowStage::Dark:
        return translated("Dark");
    case WorkflowStage::Flat:
        return translated("Flat");
    case WorkflowStage::LightsCalibration:
        return translated("Lights calibration");
    case WorkflowStage::LightsRegistration:
        return translated("Registration");
    case WorkflowStage::LightsAlignment:
        return translated("Alignment");
    case WorkflowStage::LightsNormalization:
        return translated("Normalization");
    case WorkflowStage::LightsDrizzle:
        return translated("Drizzle");
    case WorkflowStage::LightsStacking:
        return translated("Stacking");
    case WorkflowStage::None:
        break;
    }
    return {};
}

QString workflowStageToken(const WorkflowStage stage)
{
    switch (stage)
    {
    case WorkflowStage::Bias:
        return QStringLiteral("Bias");
    case WorkflowStage::Dark:
        return QStringLiteral("Dark");
    case WorkflowStage::Flat:
        return QStringLiteral("Flat");
    case WorkflowStage::LightsCalibration:
        return QStringLiteral("LightsCalibration");
    case WorkflowStage::LightsRegistration:
        return QStringLiteral("LightsRegistration");
    case WorkflowStage::LightsAlignment:
        return QStringLiteral("LightsAlignment");
    case WorkflowStage::LightsNormalization:
        return QStringLiteral("LightsNormalization");
    case WorkflowStage::LightsDrizzle:
        return QStringLiteral("LightsDrizzle");
    case WorkflowStage::LightsStacking:
        return QStringLiteral("LightsStacking");
    case WorkflowStage::None:
        break;
    }
    return {};
}

int workflowStageIndex(const QStringList& sequence, const WorkflowStage stage)
{
    const QString token = workflowStageToken(stage);
    if (token.isEmpty())
    {
        return -1;
    }
    const qsizetype exact = sequence.indexOf(token);
    if (exact >= 0)
    {
        return static_cast<int>(exact);
    }
    for (qsizetype at = 0; at < sequence.size(); ++at)
    {
        if (sequence[at].contains(token, Qt::CaseInsensitive))
        {
            return static_cast<int>(at);
        }
    }
    return -1;
}

std::vector<double> workflowStageWeights(const WorkflowManifest& manifest)
{
    const WorkflowTimeEstimator::StageSeconds seconds = WorkflowTimeEstimator::estimateStageSeconds(manifest);
    std::vector<double> weights;
    weights.reserve(static_cast<std::size_t>(manifest.stageSequence.size()));
    for (const QString& token : manifest.stageSequence)
    {
        weights.push_back(seconds.of(token));
    }
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    // A stage with nothing counted weighs a hundredth of the rest, or one where nothing at all is counted.
    const double least = total > 0.0 ? total * kLeastStageShare : 1.0;
    for (double& weight : weights)
    {
        weight = (std::max)(weight, least);
    }
    return weights;
}

WorkflowRunStanding workflowRunStanding(const QStringList& sequence, const std::vector<double>& weights,
                                        const WorkflowProgress& progress)
{
    WorkflowRunStanding standing;
    standing.steps = static_cast<int>(sequence.size());
    standing.stage = workflowStageTitle(progress.currentStage);
    standing.stageFraction = std::clamp(static_cast<double>(progress.overallProgress), 0.0, 1.0);
    const int index = workflowStageIndex(sequence, progress.currentStage);
    if (index < 0)
    {
        // A stage the run's sequence does not name: all that is known is how far that stage has come.
        standing.fraction = standing.stageFraction;
        return standing;
    }
    standing.step = index + 1;
    const std::size_t at = static_cast<std::size_t>(index);
    const bool weighed = weights.size() == static_cast<std::size_t>(sequence.size()) &&
                         std::accumulate(weights.begin(), weights.end(), 0.0) > 0.0 &&
                         std::all_of(weights.begin(), weights.end(), [](const double weight) { return weight >= 0.0; });
    if (!weighed)
    {
        standing.fraction = (static_cast<double>(index) + standing.stageFraction) / static_cast<double>(standing.steps);
        return standing;
    }
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    const double passed = std::accumulate(weights.begin(), weights.begin() + static_cast<std::ptrdiff_t>(at), 0.0);
    standing.fraction = std::clamp((passed + weights[at] * standing.stageFraction) / total, 0.0, 1.0);
    return standing;
}

QString workflowRunStandingText(const WorkflowRunStanding& standing)
{
    if (standing.step <= 0 || standing.steps <= 0)
    {
        return {};
    }
    return translated("Step %1/%2: %3").arg(standing.step).arg(standing.steps).arg(standing.stage);
}

std::optional<qint64> workflowRemainingMs(const qint64 elapsedMs, const double fraction)
{
    if (elapsedMs < kRemainingLeastElapsedMs || fraction < kRemainingLeastFraction || fraction >= 1.0)
    {
        return std::nullopt;
    }
    return static_cast<qint64>(std::llround(static_cast<double>(elapsedMs) * (1.0 - fraction) / fraction));
}

} // namespace ImageIntegration
