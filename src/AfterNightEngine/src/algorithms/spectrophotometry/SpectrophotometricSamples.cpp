// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - assembling the colour samples of every preflight match.
//
// Every match yields exactly one sample, in match order: accepted, or rejected with the first reason
// that applies and counted under it. The apertures are measured as one batch before any sample is
// judged, placed on the detected or the catalog-projected position as the request says.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricSamples.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/spectrophotometry/SpectrophotometryInternal.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using spectrophotometry_detail::failure;

constexpr const char* kOperation = "an::buildSpectrophotometricSamples";
constexpr std::uint64_t kStageCount = 3u;

/// Whether a supplied prediction is usable.
///
/// @param reference the prediction.
/// @return true when it is available and every ratio is finite and positive.
[[nodiscard]] bool usableReference(const CatalogColorReference& reference) noexcept
{
    if (!reference.available)
    {
        return false;
    }
    for (const double ratio : reference.relativeToGreen)
    {
        if (!std::isfinite(ratio) || ratio <= 0.0)
        {
            return false;
        }
    }
    return true;
}

/// Refuses one sample and counts the reason.
///
/// @param sample the sample.
/// @param reason why.
/// @param result the counts.
void reject(SpectrophotometricSample& sample, const SampleRejectionReason reason, SampleConstructionResult& result)
{
    sample.accepted = false;
    sample.rejection = reason;
    ++result.rejectedCount;
    ++result.rejections[static_cast<std::size_t>(reason)];
}

} // namespace

const char* sampleRejectionReasonName(const SampleRejectionReason reason) noexcept
{
    switch (reason)
    {
    case SampleRejectionReason::None:
        return "None";
    case SampleRejectionReason::MissingCatalogSource:
        return "MissingCatalogSource";
    case SampleRejectionReason::CatalogColorUnavailable:
        return "CatalogColorUnavailable";
    case SampleRejectionReason::ApertureRejected:
        return "ApertureRejected";
    case SampleRejectionReason::NonPositiveGreenFlux:
        return "NonPositiveGreenFlux";
    case SampleRejectionReason::NonFiniteRatio:
        return "NonFiniteRatio";
    }
    return "UnknownSampleRejectionReason";
}

Result<SampleConstructionResult>
buildSpectrophotometricSamples(const ExecutionContext& execution, const StarFrameView& frame,
                               const WcsPreflightResult& preflight, const SpectrumCatalog& catalog,
                               const std::span<const CatalogColorReference> catalogColors,
                               const SampleConstructionRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "sample construction needs an execution context", kOperation);
    }
    if (!catalog.valid())
    {
        return failure(ErrorCode::NotInitialized, "sample construction needs a built spectrum catalog", kOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (!frame.valid())
    {
        return failure(ErrorCode::InvalidArgument, "sample construction needs a checked frame", kOperation);
    }
    if (!spectrophotometry_detail::isRgbFrame(frame))
    {
        return failure(ErrorCode::Unsupported, "sample construction needs a three-channel RGB frame", kOperation);
    }
    if (!isApertureCentroidSource(request.centroidSource))
    {
        return failure(ErrorCode::InvalidArgument, "the centroid source is one this build implements", kOperation);
    }
    if (catalogColors.size() != preflight.matches.size())
    {
        return failure(ErrorCode::InvalidArgument, "one catalog prediction is supplied per preflight match",
                       kOperation);
    }
    if (request.requireAcceptableWcsQuality)
    {
        const WcsQualityVerdict verdict = checkWcsQuality(preflight.report, request.qualityThresholds);
        if (verdict != WcsQualityVerdict::Acceptable)
        {
            return failure(ErrorCode::InvalidState,
                           std::string("the world-coordinate quality gate refused: ") + wcsQualityVerdictName(verdict),
                           kOperation);
        }
    }
    if (preflight.matches.empty())
    {
        return failure(ErrorCode::EmptySelection, "the preflight produced no match to sample", kOperation);
    }

    execution.reportProgress(0u, kStageCount, "placing apertures on the matched stars");
    const std::size_t count = preflight.matches.size();
    std::vector<ApertureTarget> targets(count);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const PreflightMatch& match = preflight.matches[index];
        targets[index].position = request.centroidSource == ApertureCentroidSource::CatalogProjectedPosition
                                      ? match.projectedPosition
                                      : match.detectedPosition;
        targets[index].fullWidthPixels = match.fullWidthPixels;
    }
    std::vector<AperturePhotometryMeasurement> measurements(count);
    Result<AperturePhotometryBatchResult> measured = measureAperturePhotometryBatch(
        execution, frame, targets, request.aperture,
        request.includeNeighbors ? ApertureNeighborPolicy::OtherTargets : ApertureNeighborPolicy::None, measurements);
    if (!measured.hasValue())
    {
        return measured.status();
    }
    execution.reportProgress(1u, kStageCount, "assembling the colour samples");

    SampleConstructionResult result{};
    result.workingBytes = measured->workingBytes;
    result.samples.reserve(count);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const PreflightMatch& match = preflight.matches[index];
        SpectrophotometricSample sample{};
        sample.sourceIdentifier = match.sourceIdentifier;
        sample.detectedPosition = match.detectedPosition;
        sample.projectedPosition = match.projectedPosition;
        sample.residualPixels = match.residualPixels;
        sample.residualArcseconds = match.residualArcseconds;
        sample.aperture = measurements[index];
        sample.apertureRejection = measurements[index].rejection;

        const std::optional<std::uint32_t> record = catalog.findSourceIdentifier(match.sourceIdentifier);
        if (!record.has_value())
        {
            reject(sample, SampleRejectionReason::MissingCatalogSource, result);
            result.samples.push_back(sample);
            continue;
        }
        sample.recordIndex = *record;
        if (!usableReference(catalogColors[index]))
        {
            reject(sample, SampleRejectionReason::CatalogColorUnavailable, result);
            result.samples.push_back(sample);
            continue;
        }
        sample.catalogRelativeToGreen = catalogColors[index].relativeToGreen;
        if (sample.aperture.rejection != ApertureRejectionReason::None)
        {
            ++result.apertureRejections[static_cast<std::size_t>(sample.aperture.rejection)];
            reject(sample, SampleRejectionReason::ApertureRejected, result);
            result.samples.push_back(sample);
            continue;
        }
        const double green = sample.aperture.channels[1].flux;
        if (!std::isfinite(green) || green <= 0.0)
        {
            reject(sample, SampleRejectionReason::NonPositiveGreenFlux, result);
            result.samples.push_back(sample);
            continue;
        }
        bool finite = true;
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            const double ratio = sample.aperture.channels[channel].flux / green;
            if (!std::isfinite(ratio))
            {
                finite = false;
                break;
            }
            sample.imageRelativeToGreen[channel] = ratio;
        }
        if (!finite)
        {
            reject(sample, SampleRejectionReason::NonFiniteRatio, result);
            result.samples.push_back(sample);
            continue;
        }
        sample.accepted = true;
        ++result.acceptedCount;
        result.samples.push_back(sample);
    }
    execution.reportProgress(kStageCount, kStageCount, "colour samples assembled");
    return result;
}

} // namespace an
