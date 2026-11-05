// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What a local normalization reads off its lights before it fits them: the analysis copy its surfaces are fitted on -
/// a hot-pixel median and a noise-reduction blur when the settings ask - the figure that ranks a light as a reference,
/// and the stars whose fitted fluxes measure a light's scale against the reference.

#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace an
{
class ExecutionContext;
}

namespace ImageIntegration
{

/// How a group's local normalization analyses its lights, read from its settings.
struct LocalNormalizationAnalysis
{
    /// How the reference is chosen when no frame is named: the named frames, the best-ranked light, or the
    /// best-ranked lights' surfaces integrated.
    LocalNormReferenceStrategy strategy = LocalNormReferenceStrategy::IntegrateBestFrames;

    /// What ranks a light.
    LocalNormEvaluationCriteria criteria = LocalNormEvaluationCriteria::PsfSignalWeight;

    /// How many best-ranked lights an integrated reference holds, at least one.
    std::uint32_t maxIntegratedFrames = 16u;

    /// Whether a light's scale is the median ratio of its stars' fitted fluxes to the reference's, where at least
    /// sixteen pairs are found; otherwise the residual-ratio estimate alone.
    bool psfFluxScale = true;

    /// How stars are found and fitted: the profile model and the star limit.
    an::StarDetectionRequest detection{};

    /// The smallest amplitude over background noise a star's flux is used at.
    double minimumSignalToNoise = 0.0;

    /// Whether a star closer to another than twice the field's median width still counts.
    bool allowClusteredSources = true;

    /// The level a star's peak must stay below for its flux to count, in the frame's units.
    double highClip = 0.85;

    /// The hot-pixel median's radius, zero for none.
    std::uint32_t hotPixelMedianRadius = 0u;

    /// The noise-reduction blur's radius, the Gaussian's sigma in pixels; zero for none.
    std::uint32_t noiseReductionRadius = 0u;
};

/// The analysis a group's local normalization settings ask for.
///
/// @param settings the group's local normalization settings.
/// @return the analysis.
[[nodiscard]] LocalNormalizationAnalysis localNormalizationAnalysisFor(const LocalNormalizationSettings& settings);

/// A light's analysis samples: the light itself, or its filtered copy.
class LocalNormalizationAnalysisFrame
{
public:
    /// Prepares a light's analysis samples: a copy filtered by the hot-pixel median and then the noise-reduction blur
    /// when the analysis asks for either, the light's own samples otherwise. The light must outlive the frame.
    ///
    /// @param execution the operation the filters run under.
    /// @param analysis what the settings ask for.
    /// @param light the light, Float32 and planar.
    /// @param validity which samples mean something.
    /// @return the analysis frame; or what filtering refused.
    [[nodiscard]] static an::Result<LocalNormalizationAnalysisFrame> prepare(const an::ExecutionContext& execution,
                                                                             const LocalNormalizationAnalysis& analysis,
                                                                             const an::Image<float>& light,
                                                                             an::ValidityRule validity);

    /// Where the analysis samples start.
    [[nodiscard]] const float* samples() const noexcept;

    /// Where they are.
    [[nodiscard]] const an::ImageLayout& layout() const noexcept;

    /// What they are.
    [[nodiscard]] const an::ImageDescriptor& descriptor() const noexcept;

    /// Which samples mean something.
    [[nodiscard]] an::ValidityRule validity() const noexcept;

private:
    LocalNormalizationAnalysisFrame(const an::Image<float>& light, an::ImageLayout layout,
                                    an::ImageDescriptor descriptor, an::ValidityRule validity);

    const an::Image<float>* m_light = nullptr;
    std::vector<float> m_filtered;
    an::ImageLayout m_layout;
    an::ImageDescriptor m_descriptor;
    an::ValidityRule m_validity = an::ValidityRule::AllValid;
};

/// The channel a light's stars are searched in: the green of a colour light, the only one of a grey light.
[[nodiscard]] std::uint32_t localNormalizationDetectionChannel(const LocalNormalizationAnalysisFrame& frame) noexcept;

/// The figure that ranks a light as a reference under the analysis's criteria; larger is better.
///
/// @param execution the operation the detection runs under.
/// @param analysis the criteria and the detection.
/// @param frame the light's analysis samples.
/// @return the figure; absent when the light yields none - no star, or no finite figure; or what detecting refused.
[[nodiscard]] an::Result<std::optional<double>>
localNormalizationRankingFigure(const an::ExecutionContext& execution, const LocalNormalizationAnalysis& analysis,
                                const LocalNormalizationAnalysisFrame& frame);

/// The stars of one channel whose fluxes a scale is measured from: unsaturated, finite, inside the frame, peaking
/// below the high clip and above the signal-to-noise floor, and - unless clustered sources are allowed - no closer to
/// another than twice the field's median width.
struct LocalNormalizationStars
{
    std::vector<an::DetectedStar> stars;

    /// The field's median full width, in pixels; zero when it has none.
    double medianFullWidth = 0.0;
};

/// Detects one channel's flux stars.
///
/// @param execution the operation the detection runs under.
/// @param analysis the detection and the filters.
/// @param frame the light's analysis samples.
/// @param channel the channel.
/// @return the stars; or what detecting refused.
[[nodiscard]] an::Result<LocalNormalizationStars>
detectLocalNormalizationStars(const an::ExecutionContext& execution, const LocalNormalizationAnalysis& analysis,
                              const LocalNormalizationAnalysisFrame& frame, std::uint32_t channel);

/// The scale that brings a light's stars to the reference's: each reference star paired with the nearest unused light
/// star within three quarters of the larger median width (at least one and a half pixels), and the median ratio of
/// their fitted volumes, ratios outside (0, 100] left out.
///
/// @param reference the reference's stars.
/// @param light the light's stars.
/// @return the scale; absent below sixteen stars on either side or sixteen usable pairs.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<double>
localNormalizationFluxScale(const LocalNormalizationStars& reference, const LocalNormalizationStars& light);

} // namespace ImageIntegration
