// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a stroke, a fill or a shape lands on a coverage plane: five layer blend modes over Float32 planes.
///
/// A mask being drawn is a coverage plane - one Float32 sample per pixel, zero where nothing is selected and
/// one where everything is. A brush stroke is accumulated into a plane of its own, and what the stroke then
/// does to the mask is decided once per pixel, against the value the pixel held before the stroke began, by a
/// blend mode and an opacity. That is what makes opacity cap a whole stroke however many dabs overlap: the
/// stroke's coverage grows dab by dab, and the blend reads it once.
///
/// **Five modes.** `LayerBlendMode::Normal` moves the base towards the source's coverage;
/// `LayerBlendMode::Multiply` towards their product; `LayerBlendMode::Screen` towards one minus the product of
/// their complements; `LayerBlendMode::Overlay` multiplies a dark base and screens a light one; and
/// `LayerBlendMode::Erase` removes the source's coverage from the base. Every mode but erase then moves the base
/// by the opacity towards that target, `base + (target - base) * opacity`; erase scales the base by
/// `1 - source * opacity`.
///
/// **What a sample is read as.** A coverage is a number in the unit interval. A base or source sample below
/// zero, or one that is not a number, is read as zero, and one above one as one, so every blended sample is a
/// number within the unit interval whatever the planes held. The arithmetic is Float32 throughout, in the order
/// written above.
///
/// **A pixel the source does not cover keeps its base.** Where the source reads as zero, the destination is
/// written with the base sample unchanged, not blended: at an opacity below one a normal or multiply blend would
/// otherwise pull an uncovered pixel towards zero, and an uncovered pixel is one the stroke never touched.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// How a source's coverage combines with a base's.
enum class LayerBlendMode : std::uint8_t
{
    /// Towards the source: the target is the source's coverage.
    Normal = 0u,

    /// Towards the product of the base and the source.
    Multiply = 1u,

    /// Towards one minus the product of their complements, which never darkens.
    Screen = 2u,

    /// A base below one half multiplied by twice the source, and any other base screened with it:
    /// `2 * base * source` or `1 - 2 * (1 - base) * (1 - source)`.
    Overlay = 3u,

    /// The base scaled by one minus the source's coverage at the opacity.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Erase = 4u
};

/// Whether a value is one of the blend modes this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param mode the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isLayerBlendMode(LayerBlendMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(LayerBlendMode::Erase);
}

/// Everything a layer blend does, stated once.
///
/// A plain aggregate whose defaults are a normal blend at full opacity.
struct LayerBlendRequest
{
    /// How the source combines with the base.
    LayerBlendMode mode = LayerBlendMode::Normal;

    /// How far the base moves towards the mode's target: zero leaves it, one reaches it. Finite and
    /// within the unit interval; it is not clamped for the caller.
    float opacity = 1.0f;
};

/// A checked layer blend.
///
/// Built only by `planLayerBlend`, so holding one means the request was accepted. Copyable; it holds
/// the request and nothing else.
class LayerBlendPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const LayerBlendRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planLayerBlend` accepted.
    explicit LayerBlendPlan(const LayerBlendRequest& request) noexcept : m_request(request) {}

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<LayerBlendPlan> planLayerBlend(const LayerBlendRequest& request);

    /// The checked request.
    LayerBlendRequest m_request;
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the mode and the opacity.
/// @return the plan; or `ErrorCode::InvalidArgument` for a mode outside its enumeration or an opacity
///         that is not a number within the unit interval.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LayerBlendPlan> planLayerBlend(const LayerBlendRequest& request);

/// The blend of one base sample and one source sample.
///
/// The one-sample form of `blendLayerPlane`, for a caller whose pixels are not a plane - a flood fill
/// that blends a constant into the pixels it reaches, a gradient that blends a ramp. Unlike the plane
/// form it blends whatever the source is: a source of zero at an opacity below one still moves a normal
/// or multiply blend's base, which is what blending a fill value of zero means.
///
/// Thread-safe, allocation-free and deterministic.
///
/// @param plan the mode and the opacity.
/// @param base the value the pixel holds, read as a coverage.
/// @param source the value blended in, read as a coverage.
/// @return the blended value, a number within the unit interval.
[[nodiscard]] AFTERNIGHTENGINE_API float evaluateLayerBlend(const LayerBlendPlan& plan, float base,
                                                            float source) noexcept;

/// What one layer blend did.
struct LayerBlendResult
{
    /// Pixels the source covered, which were blended.
    std::uint64_t blended = 0u;

    /// Pixels the source did not cover, which were written with their base.
    std::uint64_t kept = 0u;
};

/// Blends a source plane into a base plane and writes the result.
///
/// Every pixel is visited once: where the source reads as covering it the destination is
/// `evaluateLayerBlend` of the base and the source, and elsewhere the base sample unchanged. The three
/// planes have one width and one height and one sample per pixel. The destination may be the base
/// itself - the same first sample and the same row stride - which blends in place; it may not overlap
/// the base any other way, and it may not overlap the source at all.
///
/// Thread-safe with respect to the base and the source, which are only read. Runs on the operation's
/// admitted threads, and the answer does not depend on how many. Allocation-free apart from a failure
/// it describes. A stop may leave earlier rows written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the mode and the opacity.
/// @param base the values the pixels held before the source.
/// @param source the coverage being blended in.
/// @param destination where the result goes.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a plane with more than one sample per pixel;
///         `ErrorCode::IncompatibleArguments` for planes of different sizes, a destination that overlaps
///         the base without being it, or one that overlaps the source; or the execution's cancellation
///         or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LayerBlendResult>
blendLayerPlane(const ExecutionContext& execution, const LayerBlendPlan& plan, PlaneView<const float> base,
                PlaneView<const float> source, PlaneView<float> destination);

} // namespace an
