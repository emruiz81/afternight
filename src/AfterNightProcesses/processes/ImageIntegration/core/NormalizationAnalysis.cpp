// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The local normalization's analysis of a light: its filtered copy, its ranking figure and its flux stars, composed of
/// the engine's order filter, Gaussian blur, star detection and star summary.

#include "stdafx.hpp"
#include "NormalizationAnalysis.h"

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Photometry/PhotometricCalibration.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Algorithms/Spatial/OrderFilter.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ImageIntegration
{

namespace
{

/// Below this many stars on either side, or pairs between them, a flux scale is not measured.
constexpr std::size_t kMinimumFluxStars = 16u;

[[nodiscard]] double medianOf(std::vector<double> values)
{
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if (values.size() % 2u != 0u)
    {
        return upper;
    }
    const double lower = *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return 0.5 * (lower + upper);
}

[[nodiscard]] an::KernelValidityView validityViewOf(const an::ValidityRule rule)
{
    an::KernelValidityView view;
    if (const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create({.rule = rule}))
    {
        view.descriptor = *descriptor;
    }
    return view;
}

[[nodiscard]] an::Result<an::StarFrameView> starFrameOf(const LocalNormalizationAnalysisFrame& frame)
{
    return an::StarFrameView::create(frame.samples(), frame.layout().totalBytes(), frame.layout(), frame.descriptor(),
                                     validityViewOf(frame.validity()));
}

[[nodiscard]] an::Result<an::StarDetectionResult> detectChannel(const an::ExecutionContext& execution,
                                                                const an::StarDetectionRequest& request,
                                                                const LocalNormalizationAnalysisFrame& frame,
                                                                const std::uint32_t channel)
{
    const an::Result<an::StarFrameView> starFrame = starFrameOf(frame);
    if (!starFrame.hasValue())
    {
        return starFrame.status();
    }
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(request, frame.descriptor(), frame.layout(), channel);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::StarDetectionResult result;
    if (const an::Status found = an::detectStars(execution, *starFrame, *plan, result); found.failed())
    {
        return found;
    }
    return result;
}

[[nodiscard]] std::uint32_t uintSetting(const int value, const std::uint32_t minimum, const std::uint32_t maximum)
{
    return static_cast<std::uint32_t>(std::clamp<long long>(
        static_cast<long long>(value), static_cast<long long>(minimum), static_cast<long long>(maximum)));
}

} // namespace

LocalNormalizationAnalysis localNormalizationAnalysisFor(const LocalNormalizationSettings& settings)
{
    LocalNormalizationAnalysis analysis;
    analysis.strategy = parseLocalNormReferenceStrategy(settings.referenceStrategy);
    analysis.criteria = parseLocalNormEvaluationCriteria(settings.evaluationCriteria);
    analysis.maxIntegratedFrames = uintSetting(settings.maxIntegratedFrames, 1u, 100000u);
    analysis.psfFluxScale = normalizeNormalizationToken(settings.scaleEvaluation) != QStringLiteral("residual ratio");

    // The profile: a Gaussian, a Moffat whose exponent is the setting's, or - automatically - a Moffat whose exponent
    // is fitted.
    const QString psf = normalizeNormalizationToken(settings.psfType);
    if (psf == QStringLiteral("gaussian"))
    {
        analysis.detection.model = an::PsfModel::Gaussian;
    }
    else if (psf.startsWith(QStringLiteral("moffat")))
    {
        analysis.detection.model = an::PsfModel::MoffatFixedBeta;
        bool parsed = false;
        const double stated = psf.mid(6).trimmed().toDouble(&parsed);
        const double beta = parsed ? stated : settings.moffatBeta;
        analysis.detection.minimumBeta = std::isfinite(beta) && beta > 0.0 ? beta : 4.0;
    }
    else
    {
        analysis.detection.model = an::PsfModel::MoffatFreeBeta;
    }
    analysis.detection.maximumStars = uintSetting(settings.maxStars, 1u, an::kMaxStarsPerFrame);

    analysis.minimumSignalToNoise =
        std::isfinite(settings.minDetectionSnr) ? std::clamp(settings.minDetectionSnr, 0.0, 1.0e6) : 0.0;
    analysis.allowClusteredSources = settings.allowClusteredSources;
    analysis.highClip = std::isfinite(settings.highClip) ? std::clamp(settings.highClip, 0.0, 1.0) : 0.85;
    analysis.hotPixelMedianRadius = uintSetting(settings.hotPixelMedianRadius, 0u, 64u);
    analysis.noiseReductionRadius = uintSetting(settings.noiseReductionRadius, 0u, 64u);
    return analysis;
}

LocalNormalizationAnalysisFrame::LocalNormalizationAnalysisFrame(const an::Image<float>& light, an::ImageLayout layout,
                                                                 an::ImageDescriptor descriptor,
                                                                 const an::ValidityRule validity)
    : m_light(&light), m_layout(std::move(layout)), m_descriptor(std::move(descriptor)), m_validity(validity)
{
}

an::Result<LocalNormalizationAnalysisFrame>
LocalNormalizationAnalysisFrame::prepare(const an::ExecutionContext& execution,
                                         const LocalNormalizationAnalysis& analysis, const an::Image<float>& light,
                                         const an::ValidityRule validity)
{
    const auto view = light.view();
    const std::optional<an::ImageDescriptor> descriptor = light.descriptor();
    if (!view.has_value() || !descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the light holds nothing");
    }
    LocalNormalizationAnalysisFrame frame(light, view->layout(), *descriptor, validity);
    if (analysis.hotPixelMedianRadius == 0u && analysis.noiseReductionRadius == 0u)
    {
        return frame;
    }

    an::SpatialSelection selection;
    selection.validity = validityViewOf(validity).descriptor;
    const an::ImageLayout& layout = frame.m_layout;
    const std::size_t samples = static_cast<std::size_t>((layout.totalBytes() + sizeof(float) - 1u) / sizeof(float));
    frame.m_filtered.assign(samples, 0.0f);
    const void* source = view->data();

    if (analysis.hotPixelMedianRadius > 0u)
    {
        an::OrderFilterRequest median;
        median.radiusX = analysis.hotPixelMedianRadius;
        median.radiusY = analysis.hotPixelMedianRadius;
        // The median reads only the samples inside the frame.
        median.border = an::KernelBorderMode::NotApplicable;
        const an::Result<an::OrderFilterResult> filtered = an::filterImageOrderStatistic(
            execution, an::SampleType::Float32, source, layout, frame.m_filtered.data(), layout, median, selection);
        if (!filtered.hasValue())
        {
            return filtered.status();
        }
        source = frame.m_filtered.data();
    }

    if (analysis.noiseReductionRadius > 0u)
    {
        an::GaussianRequest blur;
        blur.sigmaX = static_cast<double>(analysis.noiseReductionRadius);
        blur.sigmaY = blur.sigmaX;
        const an::Result<an::GaussianPlan> plan = an::planGaussianBlur(blur);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        std::vector<float> blurred(samples, 0.0f);
        const an::Result<an::SpatialFilterResult> filtered = an::blurImageGaussian(
            execution, an::SampleType::Float32, source, layout, blurred.data(), layout, *plan, selection);
        if (!filtered.hasValue())
        {
            return filtered.status();
        }
        frame.m_filtered = std::move(blurred);
    }
    return frame;
}

const float* LocalNormalizationAnalysisFrame::samples() const noexcept
{
    if (!m_filtered.empty())
    {
        return m_filtered.data();
    }
    const auto view = m_light->view();
    return view.has_value() ? view->data() : nullptr;
}

const an::ImageLayout& LocalNormalizationAnalysisFrame::layout() const noexcept
{
    return m_layout;
}

const an::ImageDescriptor& LocalNormalizationAnalysisFrame::descriptor() const noexcept
{
    return m_descriptor;
}

an::ValidityRule LocalNormalizationAnalysisFrame::validity() const noexcept
{
    return m_validity;
}

std::uint32_t localNormalizationDetectionChannel(const LocalNormalizationAnalysisFrame& frame) noexcept
{
    return frame.descriptor().extent().channelCount() >= 3u ? 1u : 0u;
}

an::Result<std::optional<double>> localNormalizationRankingFigure(const an::ExecutionContext& execution,
                                                                  const LocalNormalizationAnalysis& analysis,
                                                                  const LocalNormalizationAnalysisFrame& frame)
{
    const std::uint32_t channel = localNormalizationDetectionChannel(frame);
    if (analysis.criteria == LocalNormEvaluationCriteria::Median)
    {
        const an::Result<an::StarFrameView> starFrame = starFrameOf(frame);
        if (!starFrame.hasValue())
        {
            return starFrame.status();
        }
        const an::Result<an::StarChannelStatistics> statistics = an::measureStarChannel(execution, *starFrame, channel);
        if (!statistics.hasValue())
        {
            if (statistics.status().code() == an::ErrorCode::EmptySelection)
            {
                return std::optional<double>{};
            }
            return statistics.status();
        }
        return std::optional<double>(statistics->background);
    }

    an::StarDetectionRequest request = analysis.detection;
    request.measureRobustScale = analysis.criteria == LocalNormEvaluationCriteria::PsfSnr;
    const an::Result<an::StarDetectionResult> detected = detectChannel(execution, request, frame, channel);
    if (!detected.hasValue())
    {
        if (detected.status().code() == an::ErrorCode::EmptySelection)
        {
            return std::optional<double>{};
        }
        return detected.status();
    }
    const an::StarFieldSummary& summary = detected->summary;
    const auto finite = [](const double value) {
        return std::isfinite(value) ? std::optional<double>(value) : std::optional<double>{};
    };

    switch (analysis.criteria)
    {
    case LocalNormEvaluationCriteria::StarCount:
        return std::optional<double>(static_cast<double>(summary.starCount));
    case LocalNormEvaluationCriteria::PsfSignalWeight:
        return summary.quality.has_value() ? finite(*summary.quality) : std::optional<double>{};
    case LocalNormEvaluationCriteria::QualityScore: {
        if (summary.starCount == 0u || !summary.medianFullWidth.has_value() ||
            !summary.medianSignalToNoise.has_value() || !summary.medianRoundness.has_value())
        {
            return std::optional<double>{};
        }
        const double stars = std::log1p(static_cast<double>(summary.starCount)) * 10.0;
        const double signal = std::log1p((std::max)(0.0, *summary.medianSignalToNoise)) * 2.0;
        const double width = (std::max)(0.0, *summary.medianFullWidth) * 5.0;
        const double elongation = std::clamp(1.0 - *summary.medianRoundness, 0.0, 1.0) * 10.0;
        return finite(stars + signal - width - elongation);
    }
    case LocalNormEvaluationCriteria::PsfSnr: {
        if (detected->stars.empty() || !summary.channel.medianAbsoluteDeviation.has_value())
        {
            return std::optional<double>{};
        }
        const double noise = *summary.channel.medianAbsoluteDeviation * 1.4826 + 1.0e-12;
        std::vector<double> ratios;
        for (const an::DetectedStar& star : detected->stars)
        {
            if (!star.psf.saturated && std::isfinite(star.psf.amplitude) && star.psf.amplitude > 0.0)
            {
                ratios.push_back(star.psf.amplitude / noise);
            }
        }
        return ratios.empty() ? std::optional<double>{} : finite(medianOf(std::move(ratios)));
    }
    case LocalNormEvaluationCriteria::MStar: {
        std::vector<double> magnitudes;
        for (const an::DetectedStar& star : detected->stars)
        {
            if (!star.psf.saturated && star.instrumentalMagnitude.has_value() &&
                std::isfinite(*star.instrumentalMagnitude))
            {
                magnitudes.push_back(*star.instrumentalMagnitude);
            }
        }
        // A smaller magnitude is a brighter star, so the figure is its negative.
        return magnitudes.empty() ? std::optional<double>{} : finite(-medianOf(std::move(magnitudes)));
    }
    case LocalNormEvaluationCriteria::Median:
        break;
    }
    return std::optional<double>{};
}

an::Result<LocalNormalizationStars> detectLocalNormalizationStars(const an::ExecutionContext& execution,
                                                                  const LocalNormalizationAnalysis& analysis,
                                                                  const LocalNormalizationAnalysisFrame& frame,
                                                                  const std::uint32_t channel)
{
    LocalNormalizationStars result;
    const an::Result<an::StarDetectionResult> detected = detectChannel(execution, analysis.detection, frame, channel);
    if (!detected.hasValue())
    {
        if (detected.status().code() == an::ErrorCode::EmptySelection)
        {
            return result;
        }
        return detected.status();
    }
    result.medianFullWidth = detected->summary.medianFullWidth.value_or(0.0);
    const double width = static_cast<double>(frame.descriptor().extent().width());
    const double height = static_cast<double>(frame.descriptor().extent().height());
    for (const an::DetectedStar& star : detected->stars)
    {
        const double peak = star.psf.background + star.psf.amplitude;
        if (star.psf.saturated || !std::isfinite(star.psf.amplitude) || star.psf.amplitude <= 0.0 ||
            !std::isfinite(star.psf.background) || !std::isfinite(star.positionX) || !std::isfinite(star.positionY) ||
            star.positionX < 0.0 || star.positionY < 0.0 || star.positionX > width || star.positionY > height ||
            !std::isfinite(peak) || peak >= analysis.highClip)
        {
            continue;
        }
        if (!star.signalToNoise.has_value() || !std::isfinite(*star.signalToNoise) ||
            *star.signalToNoise < analysis.minimumSignalToNoise)
        {
            continue;
        }
        result.stars.push_back(star);
    }

    if (!analysis.allowClusteredSources && result.stars.size() > 1u)
    {
        const double separation = (std::max)(2.0, 2.0 * (std::max)(1.0, result.medianFullWidth));
        std::vector<an::DetectedStar> kept;
        kept.reserve(result.stars.size());
        for (const an::DetectedStar& star : result.stars)
        {
            const bool close = std::any_of(kept.begin(), kept.end(), [&](const an::DetectedStar& other) {
                const double dx = star.positionX - other.positionX;
                const double dy = star.positionY - other.positionY;
                return dx * dx + dy * dy < separation * separation;
            });
            if (!close)
            {
                kept.push_back(star);
            }
        }
        result.stars = std::move(kept);
    }
    return result;
}

std::optional<double> localNormalizationFluxScale(const LocalNormalizationStars& reference,
                                                  const LocalNormalizationStars& light)
{
    if (reference.stars.size() < kMinimumFluxStars || light.stars.size() < kMinimumFluxStars)
    {
        return std::nullopt;
    }
    const double radius = (std::max)(1.5, 0.75 * (std::max)(reference.medianFullWidth, light.medianFullWidth));
    std::vector<bool> used(light.stars.size(), false);
    std::vector<double> ratios;
    for (const an::DetectedStar& star : reference.stars)
    {
        double best = radius * radius;
        std::size_t match = light.stars.size();
        for (std::size_t index = 0; index < light.stars.size(); ++index)
        {
            if (used[index])
            {
                continue;
            }
            const double dx = star.positionX - light.stars[index].positionX;
            const double dy = star.positionY - light.stars[index].positionY;
            const double distance = dx * dx + dy * dy;
            if (distance < best)
            {
                best = distance;
                match = index;
            }
        }
        if (match == light.stars.size())
        {
            continue;
        }
        used[match] = true;
        const std::optional<double> referenceFlux = an::starInstrumentalFlux(star, an::StarFluxEstimator::PsfVolume);
        const std::optional<double> lightFlux =
            an::starInstrumentalFlux(light.stars[match], an::StarFluxEstimator::PsfVolume);
        if (!referenceFlux.has_value() || !lightFlux.has_value() || *lightFlux <= 0.0)
        {
            continue;
        }
        const double ratio = *referenceFlux / *lightFlux;
        if (std::isfinite(ratio) && ratio > 0.0 && ratio <= 100.0)
        {
            ratios.push_back(ratio);
        }
    }
    if (ratios.size() < kMinimumFluxStars)
    {
        return std::nullopt;
    }
    return medianOf(std::move(ratios));
}

} // namespace ImageIntegration
