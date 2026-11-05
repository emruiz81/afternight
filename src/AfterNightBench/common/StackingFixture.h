// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - matched stacking scenarios over every combination, rejection
// family, weighting source, normalization pass and tile choice, with the derived frames each is
// stacked from and the rule its oracle holds the result to.
//
// The rows are declared from this one header, so a paired comparison measures the same stack on
// both sides: the same frames, the same request and the same oracle. The plain rows
// stack eight seeded frames of the synthetic fixture, whose combination has a closed form the
// oracle computes in double. The rejection rows cannot: no engine-neutral closed form exists for
// what an iterated estimator keeps of arbitrary noise. They stack frames derived from one fixture
// so that the answer is known by construction - every frame is the same field within a symmetric
// per-frame step of a few parts in ten thousand, and each frame plants one band of gross outliers
// no family with the default thresholds can keep - and their oracle is the mean of the samples
// that were not planted, held at a tolerance that a kept outlier would exceed by more than an
// order of magnitude and that the differences between the families' treatments of the survivors
// cannot reach. The exact numerical contract of each family is proven elsewhere, by the direct
// suite's tests against recorded expectations; these rows prove that the
// complete stack ran the family it names and measure what that costs.
#pragma once

#include "Scenario.h"
#include "StackingOracle.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an::bench
{

/// The frame geometry of the synthetic stacking rows.
inline constexpr std::uint32_t kStackScenarioWidth = 1024u;
inline constexpr std::uint32_t kStackScenarioHeight = 1024u;

/// How many frames every stacking row combines.
inline constexpr std::uint32_t kStackScenarioFrames = 8u;

/// The seed of the first frame; the plain rows take frame `f` from seed plus `f`.
inline constexpr std::uint64_t kStackScenarioSeed = 20260822u;

/// How the frames of a stacking row derive from the synthetic fixture.
enum class StackFrameMapping : std::uint8_t
{
    /// Frame `f` is the fixture at seed plus `f`, unchanged.
    PerSeed,

    /// Every frame is `kStackBaseOffset + kStackBaseScale * fixture` plus its symmetric step, and
    /// frame `f` plants an outlier band over its own rows.
    PlantedOutliers,

    /// Every frame is `kStackBaseOffset + kStackBaseScale * fixture` plus a per-frame level offset
    /// a global additive normalization to frame zero removes exactly.
    PerFrameOffset,

    /// The same spatially varying frame in each slot, to exercise local weight measurement
    /// and interpolation with an exact output oracle.
    Identical,

    /// Distinct captured frames, named by the corpus id and its numbered suffixes.
    PerCorpusFrame,

    /// Distinct captured frames, each of which plants a band of the high outlier over its own rows, as a
    /// satellite trail or a glint lands in one exposure and no other.
    PlantedOutliersOnCorpus
};

/// The derived rows keep every sample inside (0, 1): the fixture's [0, 1) is mapped onto
/// [0.3, 0.6) before the per-frame terms are added, so a low outlier is below every ordinary
/// sample and a high one above, and no sample is the exact zero the engine reads as no-data.
inline constexpr float kStackBaseOffset = 0.3f;
inline constexpr float kStackBaseScale = 0.3f;

/// The per-frame symmetric step of the outlier rows: frame `f` adds `(f - 3.5) * step`, so the
/// eight ordinary samples of an element spread by seven steps around the field and their mean is
/// the field itself.
inline constexpr float kStackFrameStep = 2.0e-4f;

/// The planted outliers: an even frame plants the high value, an odd frame the low one, over the
/// `kStackOutlierRows` rows that begin at `frame * kStackOutlierRows`.
inline constexpr float kStackHighOutlier = 0.95f;
inline constexpr float kStackLowOutlier = 0.02f;
inline constexpr std::uint32_t kStackOutlierRows = 64u;

/// The per-frame level offset of the normalization row: frame `f` adds `(f - 3.5) * step`.
inline constexpr float kStackFrameOffsetStep = 0.02f;

/// The explicit weight of frame `f` in the weighted row.
[[nodiscard]] inline double stackExplicitWeight(std::uint32_t frame) noexcept
{
    return 1.0 + 0.25 * static_cast<double>(frame);
}

/// The tolerances: exact selections and means at the single-precision noise of eight terms, the
/// peak-normalized sum at twice that for its division, the auto-adaptive mean at ten times for
/// its five reweighting iterations, the rejection rows at ten times the spread of the ordinary
/// samples, and the normalization row at the estimator's own numerical noise.
inline constexpr double kStackExactTolerance = 1.0e-6;
inline constexpr double kStackSumTolerance = 2.0e-6;
inline constexpr double kStackAutoAdaptiveTolerance = 1.0e-5;
inline constexpr double kStackRejectionTolerance = 2.0e-3;
inline constexpr double kStackNormalizationTolerance = 1.0e-4;

/// The registered Carina red lights' centre crops the light stacks read; the n-th light is this id with ".n".
inline constexpr const char* kStackLightCrops = "carina.asi294mm.red.lights72.aligned.crop2048.f32";
/// The crops' side.
inline constexpr std::uint32_t kStackLightCropSide = 2048u;

/// The bound a rejection row over captured lights holds `plantedStackMovement`'s fraction to: the share of the
/// planted sky elements its stack may move by more than half what keeping the planted sample would. A family
/// that keeps the outlier moves all of them; on these eight lights the families that reject it move 0.1% to
/// 0.7%, the columns whose own captured samples hold a trail or a cosmic ray the planted
/// one tips.
inline constexpr double kStackLightRejectionTolerance = 1.0e-2;

/// One stacking row: the request the runner issues and the frames it issues it over.
struct StackScenarioDefinition
{
    const char* id; ///< The scenario id.
    const char* method; ///< The combination, in this engine's spelling.
    const char* rejection; ///< The rejection family, or "None".
    const char* weighting; ///< The weighting source or star-count scaling variant.
    const char* normalization; ///< "None" or "AdditiveGlobal".
    std::uint32_t tileRows; ///< A caller row cap, or zero for the planner's own tile.
    StackFrameMapping mapping; ///< How the frames derive from the fixture.
    double tolerance; ///< The oracle tolerance.
    const char* corpusId = nullptr; ///< First real frame id, or null for the seeded fixture.
    std::uint32_t width = kStackScenarioWidth; ///< Frame width in pixels.
    std::uint32_t height = kStackScenarioHeight; ///< Frame height in pixels.
    std::uint32_t frames = kStackScenarioFrames; ///< How many frames the row combines.
    std::uint32_t memoryBudgetMegabytes = 0u; ///< The working memory the stack may take, or zero for none stated.
};

/// Every stacking row: every combination, rejection family and weight source, the global
/// normalization pass, a caller-capped tile and two full-resolution captured-flat integrations.
inline constexpr std::array<StackScenarioDefinition, 39u> kStackScenarioDefinitions{{
    {"stacking.average.8x_f32", "Average", "None", "Uniform", "None", 0u, StackFrameMapping::PerSeed,
     kStackExactTolerance},
    {"stacking.sum.8x_f32", "Sum", "None", "Uniform", "None", 0u, StackFrameMapping::PerSeed, kStackSumTolerance},
    {"stacking.median.8x_f32", "Median", "None", "Uniform", "None", 0u, StackFrameMapping::PerSeed,
     kStackExactTolerance},
    {"stacking.minimum.8x_f32", "Minimum", "None", "Uniform", "None", 0u, StackFrameMapping::PerSeed,
     kStackExactTolerance},
    {"stacking.maximum.8x_f32", "Maximum", "None", "Uniform", "None", 0u, StackFrameMapping::PerSeed,
     kStackExactTolerance},
    {"stacking.auto_adaptive.8x_f32", "AutoAdaptiveWeightedAverage", "None", "Uniform", "None", 0u,
     StackFrameMapping::PerSeed, kStackAutoAdaptiveTolerance},
    {"stacking.weighted_average.explicit.8x_f32", "Average", "None", "Explicit", "None", 0u, StackFrameMapping::PerSeed,
     kStackExactTolerance},
    {"stacking.rejection.sigma_clipping.8x_f32", "Average", "SigmaClipping", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliers, kStackRejectionTolerance},
    {"stacking.rejection.mad_sigma_clipping.8x_f32", "Average", "MadSigmaClipping", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliers, kStackRejectionTolerance},
    {"stacking.rejection.median_sigma_replacement.8x_f32", "Average", "MedianSigmaReplacement", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliers, kStackRejectionTolerance},
    {"stacking.rejection.winsorized_sigma_clipping.8x_f32", "Average", "WinsorizedSigmaClipping", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliers, kStackRejectionTolerance},
    {"stacking.rejection.linear_fit_clipping.8x_f32", "Average", "LinearFitClipping", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliers, kStackRejectionTolerance},
    {"stacking.rejection.percentile_clipping.8x_f32", "Average", "PercentileClipping", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliers, kStackRejectionTolerance},
    {"stacking.rejection.gesdt.8x_f32", "Average", "GeneralizedEsdTest", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliers, kStackRejectionTolerance},
    {"stacking.average.normalized_additive.8x_f32", "Average", "None", "Uniform", "AdditiveGlobal", 0u,
     StackFrameMapping::PerFrameOffset, kStackNormalizationTolerance},
    {"stacking.average.tiled_32rows.8x_f32", "Average", "None", "Uniform", "None", 32u, StackFrameMapping::PerSeed,
     kStackExactTolerance},
    {"stacking.weighted_average.noise.8x_f32", "Average", "None", "Noise", "None", 0u, StackFrameMapping::PerSeed,
     kStackExactTolerance},
    {"stacking.weighted_average.star_count_linear.8x_f32", "Average", "None", "StarCountLinear", "None", 0u,
     StackFrameMapping::PerSeed, kStackExactTolerance},
    {"stacking.weighted_average.star_count_quadratic.8x_f32", "Average", "None", "StarCountQuadratic", "None", 0u,
     StackFrameMapping::PerSeed, kStackExactTolerance},
    {"stacking.weighted_average.star_count_exponential.8x_f32", "Average", "None", "StarCountExponential", "None", 0u,
     StackFrameMapping::PerSeed, kStackExactTolerance},
    {"stacking.weighted_average.star_count_roundness.8x_f32", "Average", "None", "StarCountRoundness", "None", 0u,
     StackFrameMapping::PerSeed, kStackExactTolerance},
    {"stacking.weighted_average.fwhm.8x_f32", "Average", "None", "Fwhm", "None", 0u, StackFrameMapping::PerSeed,
     kStackExactTolerance},
    {"stacking.weighted_average.image_count.8x_f32", "Average", "None", "ImageCount", "None", 0u,
     StackFrameMapping::PerSeed, kStackExactTolerance},
    {"stacking.weighted_average.photometric.8x_f32", "Average", "None", "Photometric", "None", 0u,
     StackFrameMapping::PerSeed, kStackExactTolerance},
    {"stacking.weighted_average.local_grid.8x_f32", "Average", "None", "Local", "None", 0u,
     StackFrameMapping::Identical, kStackExactTolerance},
    {"stacking.average.carina_red_flats.8x_f32", "Average", "None", "Uniform", "None", 0u,
     StackFrameMapping::PerCorpusFrame, kStackExactTolerance, "carina.asi294mm.red.flats8.f32", 8288u, 5644u},
    {"stacking.median.carina_red_flats.8x_f32", "Median", "None", "Uniform", "None", 0u,
     StackFrameMapping::PerCorpusFrame, kStackExactTolerance, "carina.asi294mm.red.flats8.f32", 8288u, 5644u},
    {"stacking.average.carina_red_lights.8x_f32", "Average", "None", "Uniform", "None", 0u,
     StackFrameMapping::PerCorpusFrame, kStackExactTolerance, kStackLightCrops, kStackLightCropSide,
     kStackLightCropSide, 8u},
    {"stacking.average.carina_red_lights.32x_f32", "Average", "None", "Uniform", "None", 0u,
     StackFrameMapping::PerCorpusFrame, kStackExactTolerance, kStackLightCrops, kStackLightCropSide,
     kStackLightCropSide, 32u},
    {"stacking.average.carina_red_lights.72x_f32", "Average", "None", "Uniform", "None", 0u,
     StackFrameMapping::PerCorpusFrame, kStackExactTolerance, kStackLightCrops, kStackLightCropSide,
     kStackLightCropSide, 72u},
    {"stacking.median.carina_red_lights.32x_f32", "Median", "None", "Uniform", "None", 0u,
     StackFrameMapping::PerCorpusFrame, kStackExactTolerance, kStackLightCrops, kStackLightCropSide,
     kStackLightCropSide, 32u},
    {"stacking.weighted_average.explicit.carina_red_lights.32x_f32", "Average", "None", "Explicit", "None", 0u,
     StackFrameMapping::PerCorpusFrame, kStackExactTolerance, kStackLightCrops, kStackLightCropSide,
     kStackLightCropSide, 32u},
    {"stacking.rejection.sigma_clipping.carina_red_lights.8x_f32", "Average", "SigmaClipping", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliersOnCorpus, kStackLightRejectionTolerance, kStackLightCrops, kStackLightCropSide,
     kStackLightCropSide, 8u},
    {"stacking.rejection.mad_sigma_clipping.carina_red_lights.8x_f32", "Average", "MadSigmaClipping", "Uniform", "None",
     0u, StackFrameMapping::PlantedOutliersOnCorpus, kStackLightRejectionTolerance, kStackLightCrops,
     kStackLightCropSide, kStackLightCropSide, 8u},
    {"stacking.rejection.median_sigma_replacement.carina_red_lights.8x_f32", "Average", "MedianSigmaReplacement",
     "Uniform", "None", 0u, StackFrameMapping::PlantedOutliersOnCorpus, kStackLightRejectionTolerance, kStackLightCrops,
     kStackLightCropSide, kStackLightCropSide, 8u},
    {"stacking.rejection.winsorized_sigma_clipping.carina_red_lights.8x_f32", "Average", "WinsorizedSigmaClipping",
     "Uniform", "None", 0u, StackFrameMapping::PlantedOutliersOnCorpus, kStackLightRejectionTolerance, kStackLightCrops,
     kStackLightCropSide, kStackLightCropSide, 8u},
    {"stacking.rejection.linear_fit_clipping.carina_red_lights.8x_f32", "Average", "LinearFitClipping", "Uniform",
     "None", 0u, StackFrameMapping::PlantedOutliersOnCorpus, kStackLightRejectionTolerance, kStackLightCrops,
     kStackLightCropSide, kStackLightCropSide, 8u},
    {"stacking.rejection.percentile_clipping.carina_red_lights.8x_f32", "Average", "PercentileClipping", "Uniform",
     "None", 0u, StackFrameMapping::PlantedOutliersOnCorpus, kStackLightRejectionTolerance, kStackLightCrops,
     kStackLightCropSide, kStackLightCropSide, 8u},
    {"stacking.rejection.gesdt.carina_red_lights.8x_f32", "Average", "GeneralizedEsdTest", "Uniform", "None", 0u,
     StackFrameMapping::PlantedOutliersOnCorpus, kStackLightRejectionTolerance, kStackLightCrops, kStackLightCropSide,
     kStackLightCropSide, 8u},
}};

/// The measured figures supplied to the engine; their derivation is timed.
struct StackScenarioMeasurement
{
    std::uint32_t stars;
    double roundness;
    double fwhm;
    double noise;
    std::uint32_t integratedFrames;
    double zeroPoint;
};

[[nodiscard]] inline StackScenarioMeasurement stackScenarioMeasurement(std::uint32_t frame) noexcept
{
    return {100u + 20u * frame,   0.7 + 0.03 * frame, 2.0 + 0.2 * frame,
            0.01 + 0.002 * frame, frame + 1u,         20.0 + 0.1 * frame};
}

/// Closed-form frame weights, independently evaluated outside the timed engine call.
[[nodiscard]] inline std::vector<double> stackScenarioWeights(std::string_view source,
                                                              std::uint32_t frames = kStackScenarioFrames)
{
    std::vector<double> weights(frames, 1.0);
    const auto figure = [&](std::uint32_t frame) {
        const auto m = stackScenarioMeasurement(frame);
        if (source == "Fwhm")
        {
            return (std::max)(m.fwhm + 2.0 * m.fwhm * (100.0 - m.stars) / m.stars, m.fwhm * 0.1);
        }
        return static_cast<double>(m.stars) * (source == "StarCountRoundness" ? m.roundness : 1.0);
    };
    double low = figure(0u), high = low;
    for (std::uint32_t frame = 1u; frame < frames; ++frame)
    {
        low = (std::min)(low, figure(frame));
        high = (std::max)(high, figure(frame));
    }
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        const auto m = stackScenarioMeasurement(frame);
        double& weight = weights[frame];
        if (source == "Explicit")
            weight = stackExplicitWeight(frame);
        else if (source == "Noise")
            weight = 1.0 / (m.noise * m.noise);
        else if (source == "ImageCount")
            weight = m.integratedFrames;
        else if (source == "Photometric")
            weight = std::pow(10.0, 0.4 * (m.zeroPoint - 20.0));
        else if (source == "Fwhm")
        {
            const double value = figure(frame);
            weight = (std::max)(0.01, (1.0 / (value * value) - 1.0 / (high * high)) /
                                          (1.0 / (low * low) - 1.0 / (high * high)));
        }
        else if (source.starts_with("StarCount"))
        {
            const double unit = (figure(frame) - low) / (high - low);
            weight = source == "StarCountLinear"        ? unit
                     : source == "StarCountExponential" ? std::exp(-2.0 * (1.0 - unit))
                                                        : unit * unit;
            weight = (std::max)(0.01, weight);
        }
    }
    return weights;
}

[[nodiscard]] inline std::string stackScenarioFixtureId(const ScenarioSpec& scenario, std::uint32_t frame)
{
    return scenario.inputSource == InputSource::Corpus && frame != 0u ? scenario.corpusId + "." + std::to_string(frame)
                                                                      : scenario.corpusId;
}

/// The name a mapping is declared under.
[[nodiscard]] inline const char* stackFrameMappingName(StackFrameMapping mapping) noexcept
{
    switch (mapping)
    {
    case StackFrameMapping::PerSeed:
        return "per-seed";
    case StackFrameMapping::PlantedOutliers:
        return "planted-outliers-v1";
    case StackFrameMapping::PerFrameOffset:
        return "per-frame-offset-v1";
    case StackFrameMapping::Identical:
        return "identical-v1";
    case StackFrameMapping::PerCorpusFrame:
        return "per-corpus-frame-v1";
    case StackFrameMapping::PlantedOutliersOnCorpus:
        return "planted-outliers-corpus-v1";
    }
    return "unknown";
}

/// The rows.
[[nodiscard]] inline std::vector<ScenarioSpec> stackingModeScenarios()
{
    std::vector<ScenarioSpec> scenarios;
    scenarios.reserve(kStackScenarioDefinitions.size());
    for (const StackScenarioDefinition& definition : kStackScenarioDefinitions)
    {
        ScenarioSpec scenario;
        scenario.id = definition.id;
        scenario.group = "stacking";
        scenario.operationVersion = 1;
        scenario.inputSource = definition.corpusId == nullptr ? InputSource::Synthetic : InputSource::Corpus;
        scenario.input.width = definition.width;
        scenario.input.height = definition.height;
        scenario.input.channels = 1u;
        scenario.input.sampleType = SampleType::Float32;
        scenario.input.layout = Layout::Planar;
        // Explicit rather than native, so a fixture digest means the same thing on either host.
        scenario.input.byteOrder = ByteOrder::Little;
        scenario.corpusId = definition.corpusId == nullptr ? "synthetic.uniform_noise" : definition.corpusId;
        scenario.seed = kStackScenarioSeed;
        scenario.correctnessClass = CorrectnessClass::AbsoluteRelative;
        scenario.tolerance = definition.tolerance;
        scenario.parameters = {{"frames", std::to_string(definition.frames)},
                               {"method", definition.method},
                               {"rejection", definition.rejection},
                               {"weighting", definition.weighting},
                               {"normalization", definition.normalization},
                               {"tileRows", std::to_string(definition.tileRows)},
                               {"fixtureMapping", stackFrameMappingName(definition.mapping)}};
        scenario.elementsProcessed =
            static_cast<std::uint64_t>(definition.width) * definition.height * definition.frames;
        if (definition.memoryBudgetMegabytes != 0u)
        {
            scenario.parameters["memoryBudgetMegabytes"] = std::to_string(definition.memoryBudgetMegabytes);
        }
        scenario.bytesProcessed = scenario.elementsProcessed * sizeof(float);
        scenario.workUnit = "frames";
        scenario.allowedBackends = {BackendId::Unspecified};
        scenarios.push_back(std::move(scenario));
    }
    return scenarios;
}

/// The definition a declared row is an instance of, by every declared field except the timed
/// batch, or nothing when a runner is handed a row this header did not declare.
[[nodiscard]] inline std::optional<StackScenarioDefinition> stackingModeScenarioDefinition(const ScenarioSpec& scenario)
{
    const std::vector<ScenarioSpec> expected = stackingModeScenarios();
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        ParameterSet parameters = scenario.parameters;
        parameters.erase(kTimedBatchParameter);
        if (scenario.id == expected[index].id && scenario.input == expected[index].input &&
            parameters == expected[index].parameters && scenario.inputSource == expected[index].inputSource &&
            scenario.corpusId == expected[index].corpusId && scenario.seed == expected[index].seed)
        {
            return kStackScenarioDefinitions[index];
        }
    }
    return std::nullopt;
}

/// The rows frame `frame` plants its outliers over, clipped to the frame.
[[nodiscard]] inline StackRowRange stackOutlierRows(std::uint32_t frame, std::uint32_t height) noexcept
{
    const std::uint64_t begin = static_cast<std::uint64_t>(frame) * kStackOutlierRows;
    const std::uint64_t end = begin + kStackOutlierRows;
    return StackRowRange{.begin = static_cast<std::uint32_t>(begin < height ? begin : height),
                         .end = static_cast<std::uint32_t>(end < height ? end : height)};
}

/// The per-frame term of a derived mapping: the symmetric step or the level offset.
[[nodiscard]] inline float stackFrameTerm(StackFrameMapping mapping, std::uint32_t frame) noexcept
{
    const float centred = static_cast<float>(frame) - (static_cast<float>(kStackScenarioFrames) - 1.0f) * 0.5f;
    return mapping == StackFrameMapping::PerFrameOffset ? centred * kStackFrameOffsetStep : centred * kStackFrameStep;
}

/// Frame `frame` of a derived mapping, from the fixture's samples.
///
/// The mapping is applied in single precision in this order - the field, then the frame's term,
/// then the planted band - so every run derives byte-identical frames and the digest it reports
/// over the derived set proves it.
///
/// @param mapping which derivation; `PerSeed` derives nothing and returns the fixture unchanged, and
///     `PlantedOutliersOnCorpus` plants its band into the frame's own capture, which `fixture` is then.
/// @param fixture the fixture's samples, `width * height` of them.
/// @param frame which frame.
/// @param width the frame width.
/// @param height the frame height.
/// @return the frame's samples.
[[nodiscard]] inline std::vector<float> deriveStackFrame(StackFrameMapping mapping, std::span<const float> fixture,
                                                         std::uint32_t frame, std::uint32_t width, std::uint32_t height)
{
    std::vector<float> samples(fixture.begin(), fixture.end());
    if (mapping == StackFrameMapping::PerSeed || mapping == StackFrameMapping::Identical ||
        mapping == StackFrameMapping::PerCorpusFrame)
    {
        return samples;
    }
    if (mapping == StackFrameMapping::PlantedOutliersOnCorpus)
    {
        const StackRowRange rows = stackOutlierRows(frame, height);
        for (std::uint32_t row = rows.begin; row < rows.end; ++row)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                samples[static_cast<std::size_t>(row) * width + x] = kStackHighOutlier;
            }
        }
        return samples;
    }
    const float term = stackFrameTerm(mapping, frame);
    for (float& sample : samples)
    {
        sample = kStackBaseOffset + kStackBaseScale * sample + term;
    }
    if (mapping == StackFrameMapping::PlantedOutliers)
    {
        const StackRowRange rows = stackOutlierRows(frame, height);
        const float planted = frame % 2u == 0u ? kStackHighOutlier : kStackLowOutlier;
        for (std::uint32_t row = rows.begin; row < rows.end; ++row)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                samples[static_cast<std::size_t>(row) * width + x] = planted;
            }
        }
    }
    return samples;
}

/// The residual of a stacked plane against the row's oracle.
///
/// A normalized row is held to its reference frame, a rejection row to the mean of what was not
/// planted, and every other row to the closed form of its combination over the frames as given.
///
/// @param definition the row.
/// @param frames the frames the stack was given, one tight plane each.
/// @param width the frame width.
/// @param height the frame height.
/// @param result the stacked plane.
/// @return the largest absolute residual, or infinity when the row names no oracle.
[[nodiscard]] inline double stackScenarioResidual(const StackScenarioDefinition& definition,
                                                  std::span<const std::span<const float>> frames, std::uint32_t width,
                                                  std::uint32_t height, std::span<const float> result)
{
    const std::string_view method = definition.method;
    // A row over captured lights accepts an output held to the unit interval.
    const StackClamp clamp = definition.corpusId != nullptr && std::string_view(definition.corpusId) == kStackLightCrops
                                 ? StackClamp::Accepted
                                 : StackClamp::Refused;
    if (definition.mapping == StackFrameMapping::PlantedOutliersOnCorpus)
    {
        // Held to the same engine's stack of the lights as captured, through plantedStackMovement.
        return std::numeric_limits<double>::infinity();
    }
    if (std::string_view(definition.normalization) == "AdditiveGlobal")
    {
        return frames.empty() ? std::numeric_limits<double>::infinity() : planeResidual(frames[0], result);
    }
    if (std::string_view(definition.rejection) != "None")
    {
        std::vector<StackRowRange> excluded;
        excluded.reserve(frames.size());
        for (std::uint32_t frame = 0u; frame < frames.size(); ++frame)
        {
            excluded.push_back(stackOutlierRows(frame, height));
        }
        return rejectedAverageStackResidual(frames, width, excluded, result);
    }
    if (std::string_view(definition.weighting) != "Uniform" && std::string_view(definition.weighting) != "Local")
    {
        const std::vector<double> weights =
            stackScenarioWeights(definition.weighting, static_cast<std::uint32_t>(frames.size()));
        return weightedAverageStackResidual(frames, weights, result, clamp);
    }
    if (method == "Average")
    {
        return averageStackResidual(frames, result, clamp);
    }
    if (method == "Sum")
    {
        return sumStackResidual(frames, result);
    }
    if (method == "Median")
    {
        return medianStackResidual(frames, result, clamp);
    }
    if (method == "Minimum")
    {
        return minimumStackResidual(frames, result);
    }
    if (method == "Maximum")
    {
        return maximumStackResidual(frames, result);
    }
    if (method == "AutoAdaptiveWeightedAverage")
    {
        return autoAdaptiveStackResidual(frames, result);
    }
    return std::numeric_limits<double>::infinity();
}

} // namespace an::bench
