// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The drizzle sequence model: which samples a sequence holds, how they are weighted and judged
/// valid, which kernel spreads them and what output they produce, checked once before any sample is
/// read.
///
/// `DrizzleRequest` gathers the facts: the frames' shared `ImageDescriptor` and validity rule, the
/// optional pixel- and flat-weight descriptions, the `DrizzleParameters` every frame shares, and the
/// `DrizzleOutputRequest`. `DrizzleModel::create` refuses the combinations that cannot describe a
/// drizzle and resolves the output description; the model owns its copy and borrows no image, so the
/// planner, the map descriptors, the accumulation and the executor all read one checked value.
/// Nothing is inferred from a C++ type: the input's numeric domain, colour basis and CFA pattern and
/// phase are stated by its descriptor, and a CFA source is routed through that pattern to three RGB
/// output planes.
///
/// The numeric contract is a weighted mean of flux scaled by `1 / (scale * scale)` in Float64 terms.
#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <array>
#include <cstdint>
#include <optional>

namespace an
{

/// How an input pixel distributes its flux over the destination.
enum class DrizzleKernel : std::uint8_t
{
    Square, ///< Overlap of the four mapped drop corners with each output pixel.
    Point, ///< All flux goes to the nearest output pixel; the drop fraction is still validated.
    Turbo, ///< Axis-aligned rectangle of width `pixelFraction * scale` in output pixels.
    Gaussian, ///< Gaussian with FWHM `pixelFraction * scale`, truncated at three sigma.
    Lanczos2, ///< Separable sinc window of order two; signed lobes are retained.
    Lanczos3 ///< Separable sinc window of order three; signed lobes are retained.
};

/// Tests membership without allocating; thread-safe.
/// @param kernel the value to inspect.
/// @return whether it names a supported kernel.
[[nodiscard]] constexpr bool isDrizzleKernel(DrizzleKernel kernel) noexcept
{
    return static_cast<std::uint8_t>(kernel) <= static_cast<std::uint8_t>(DrizzleKernel::Lanczos3);
}

/// Names a kernel without allocating; thread-safe.
/// @param kernel the value to name.
/// @return a permanent literal, or `"UnknownDrizzleKernel"` for an unknown value.
[[nodiscard]] AFTERNIGHTENGINE_API const char* drizzleKernelName(DrizzleKernel kernel) noexcept;

/// How a non-finite sample that the source validity considers valid is handled.
enum class DrizzleNonFinitePolicy : std::uint8_t
{
    Reject, ///< Fail the operation before publishing a result.
    Exclude ///< Exclude it and count it; finite zero remains data unless validity says otherwise.
};

/// The frame ceiling shared with stacking's frame tables and count maps.
inline constexpr std::uint32_t kMaxDrizzleFrames = 100000u;

/// Parameters common to every frame; weights multiply and never change the sample's numeric domain.
struct DrizzleParameters
{
    DrizzleKernel kernel = DrizzleKernel::Square; ///< Distribution rule.
    double scale = 2.0; ///< Equal-axis enlargement, finite and in [1, 4].
    double pixelFraction = 0.9; ///< Drop width in input pixels, finite and in (0, 1].
    double weightScale = 1.0; ///< Positive finite global weight multiplier.
    std::array<double, 3> channelWeights{1.0, 1.0, 1.0}; ///< Positive finite output-channel multipliers.
    DrizzleNonFinitePolicy nonFinite = DrizzleNonFinitePolicy::Reject; ///< Policy after source validity.
    bool collectMaximumWeight = false; ///< Report the largest signed total weight, reduced once at the end.
};

/// How the complete sequence's signed weighted terms become output samples.
enum class DrizzleCombination : std::uint8_t
{
    WeightedMean, ///< Divide each numerator by its positive support; retain the source numeric domain.
    PeakNormalizedSum ///< Divide supported numerators by their shared positive peak and clamp to [0,1].
};

/// A description of the destination, independent of any codec or source output intent.
struct DrizzleOutputRequest
{
    SampleType sampleType = SampleType::Float32; ///< Float32 by default; Float64 may be requested explicitly.
    /// Explicit canvas, or `ceil(input dimensions * scale)` when absent; its channel count must be the
    /// source's plane count, or three for a CFA source.
    std::optional<ImageExtent> extent{};
    bool weightMap = false; ///< Produce a Float32 sum-of-weights map beside the result.
    bool coverageMap = false; ///< Produce a UInt32 count of distinct contributing frames per pixel/plane.
    DrizzleCombination combination = DrizzleCombination::WeightedMean; ///< Complete-sequence reduction.
};

/// Facts needed to validate a homogeneous sequence, without reading a sample.
///
/// Each frame has this descriptor and validity; different transforms are supplied separately.
/// Pixel weights and flats have the input's width, height, coordinate origin and pixel aspect, and
/// either one channel (broadcast) or the input channel count. They hold dimensionless Float32 or
/// Float64 values in NormalizedLinear, with no CFA and no colour space: weights are finite and
/// non-negative, flats finite and strictly positive, checked when read. The effective pixel weight
/// is the input weight times the flat, global, channel and explicit frame weights. Unused channel
/// multipliers must equal one. CFA routing reads the descriptor's pattern and phase.
struct DrizzleRequest
{
    ImageDescriptor
        input; ///< Owned description; one to four planes or an unresolved CFA sensor plane, with top-left rows.
    std::uint32_t frameCount = 1u; ///< Number of frames, in [1, kMaxDrizzleFrames].
    ValidityDescriptor validity{}; ///< Source validity; never inferred from zero samples.
    std::optional<ImageExtent> maskExtent{}; ///< Exact input extent iff validity requires a packed mask.
    std::optional<ImageDescriptor> pixelWeights{}; ///< Optional per-frame pixel-weight description.
    std::optional<ImageDescriptor> flatWeights{}; ///< Optional flat-weight description; pixels are supplied later.
    DrizzleParameters parameters{}; ///< Sequence parameters.
    DrizzleOutputRequest output{}; ///< Requested result and optional maps.
};

/// Immutable checked sequence model; copies own every field and may be read concurrently.
///
/// The default output is a weighted mean of flux scaled by 1/(scale*scale), without clipping or domain
/// conversion. PeakNormalizedSum instead normalizes the supported weighted numerators by one
/// positive peak across every channel and destination band, then clamps to [0,1]. Without a
/// positive peak the finite sums are retained. Its output domain is NormalizedLinear (or the
/// input's EncodedNonlinear domain). The reduction precedes narrowing to the requested sample type.
/// Finalization maps normalized integer full scale into the floating representation,
/// without integer requantization; raw sensor code values retain their units.
/// Weighted sums and signed weight sums accumulate in Float64 for
/// Float64Justification::StackingAccumulation. A destination with no positive finite total weight
/// receives zero and an invalid mask bit, including cancellation of Lanczos lobes. Validity is
/// regenerated per output plane; a black pixel with positive support is valid. No output or artifact
/// is published after a failure. Input acquisition facts are preserved, lineage is appended, and
/// input astrometry and output intent are withdrawn because they describe the source geometry.
class DrizzleModel
{
public:
    /// Validates parameters, meaning, auxiliary geometry and output limits; reads no sample.
    /// Thread-safe; allocates only a returned error detail. Does not reserve memory.
    /// @param request the complete sequence description.
    /// @param limits geometry and per-image byte ceilings; the planner rechecks its execution's limits.
    /// @return the owned model; `ErrorCode::InvalidArgument` for an unknown kernel, combination or
    ///         non-finite policy, a scale, drop fraction, global or channel weight outside its range,
    ///         a frame count outside [1, kMaxDrizzleFrames], or an output type other than Float32 or
    ///         Float64; `ErrorCode::IncompatibleArguments` for an unused channel weight other than
    ///         one, a mask extent that is missing, unwanted or not the input's, auxiliary weights
    ///         whose geometry or meaning differs from the source's, an explicit canvas with the wrong
    ///         channel count, or an output that describes no image; `ErrorCode::Unsupported` for rows
    ///         that do not start at the top left or more than four source channels; or
    ///         `ErrorCode::LimitExceeded` when the source, an auxiliary plane or the output exceeds
    ///         `limits`.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DrizzleModel>
    create(const DrizzleRequest& request, const EngineLimits& limits = defaultEngineLimits());

    /// The validated request.
    /// @return a borrow valid for this model's lifetime, invalidated by assignment.
    [[nodiscard]] const DrizzleRequest& request() const noexcept
    {
        return m_request;
    }

    /// The resolved destination description; a CFA source becomes three RGB planes with no CFA.
    /// @return a borrow valid for this model's lifetime, invalidated by assignment.
    [[nodiscard]] const ImageDescriptor& outputDescriptor() const noexcept
    {
        return m_output;
    }

    /// Whether source samples route through their CFA colour to separate output planes.
    /// @return true for an unresolved CFA source.
    [[nodiscard]] bool usesCfa() const noexcept
    {
        return m_request.input.cfa().pattern() != CfaPattern::None;
    }

private:
    /// Stores checked values without allocating.
    /// @param request the checked request.
    /// @param output the resolved output.
    DrizzleModel(const DrizzleRequest& request, const ImageDescriptor& output) : m_request(request), m_output(output) {}
    DrizzleRequest m_request; ///< Owned checked description.
    ImageDescriptor m_output; ///< Resolved destination meaning.
};

/// The operation's metadata and validity effects; thread-safe and allocation-free. The lineage
/// entry is not part of the effect: `drizzleTransition` supplies it.
/// @return samples written, a restated description and processing meaning, an invalidated
///         astrometric solution, dropped source header cards, withdrawn output intent, and validity
///         regenerated as an explicit mask.
[[nodiscard]] AFTERNIGHTENGINE_API OperationEffect drizzleOperationEffect() noexcept;

} // namespace an
