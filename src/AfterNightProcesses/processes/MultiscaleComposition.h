// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The multiscale linear transform window's work, composed from the engine's published operations: the transform
/// itself, per-layer gains and noise reduction, the global k-sigma shrinkage, the linear mask that modulates both,
/// the deringing that attenuates coefficients at edges and its maps, the large-scale transfer of the residual, the
/// dynamic-range extension, and the previews of one layer and of the linear mask.
///
/// **The engine owns the transform; the window owns what it does with the layers.** `decomposeImageMultiscale`,
/// `shrinkMultiscaleLayer` and `reconstructImageMultiscale` are the transform, its shrinkage and its weighted sum. The
/// window's decisions around them - which the engine leaves to its consumer to compose - are arithmetic over layer
/// planes written as PixelMath expressions, pointwise programs, the exact Gaussian and order statistics.
///
/// **Every plane is worked in fractions of full scale and in single precision**: a channel's samples as a fraction of
/// its range, the layers Float32 coefficients, the result held to the unit interval unless a dynamic-range extension
/// states another mapping.
///
/// **The noise of each layer is derived from the finest one**: the finest layer's deviation around zero, optionally
/// refined by the multiresolution support, scaled to each layer by the share of white noise that layer's filter
/// passes. Those shares are the norms of each layer's impulse response: exact, where an estimate from random draws
/// would depend on a generator the standard library leaves to each implementation.
#pragma once

#include "AfterNightProcessesExport.h"

#include "session/ExecutionScope.h"

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace process_utils::multiscale
{

/// What one layer of the transform contributes: whether it is kept, its bias, and its noise reduction.
struct LayerSettings
{
    /// Whether the layer is kept at all.
    bool enabled = true;

    /// The bias: zero keeps the layer as it is, a positive bias amplifies it and a negative one attenuates it.
    double bias = 0.0;

    /// Whether the layer's coefficients are shrunk as noise. Never for the residual.
    bool noiseReduction = false;

    /// The noise threshold, in the dialog's units: twenty of them make one deviation of the layer's noise.
    double noiseThreshold = 3.0;

    /// How much of the shrinkage is applied, within [0, 1].
    double noiseAmount = 0.5;

    /// How many times the shrinkage is applied.
    int noiseIterations = 1;
};

/// How the residual's large scales are compressed before the layers are added back.
enum class ResidualTransfer
{
    None,
    Hyperbolic,
    Log10,
    NaturalLog
};

/// What a preview of one layer shows of its coefficients, on a grey of one half or black.
enum class LayerPreview
{
    /// Every coefficient, brighter where it adds and darker where it removes.
    AllChanges,

    /// The coefficients that add, brighter the more they add.
    IncreasingPixels,

    /// The coefficients that remove, brighter the more they remove.
    DecreasingPixels
};

/// Everything the window's dialog states about one run.
struct TransformRequest
{
    an::MultiscaleAlgorithm algorithm = an::MultiscaleAlgorithm::MultiscaleLinear;
    an::MultiscaleScalingFunction scaling = an::MultiscaleScalingFunction::BSpline5;
    an::MultiscaleLayerSpacing spacing = an::MultiscaleLayerSpacing::Dyadic;
    std::uint32_t linearScale = 1u;
    std::uint32_t detailLayers = 6u;

    /// One entry per detail layer and the residual last; missing entries keep their layer as it is.
    std::vector<LayerSettings> layers;

    bool kSigma = false;
    double kSigmaK = 3.0;
    double kSigmaAmount = 0.5;
    bool kSigmaSoft = true;
    bool kSigmaMultiresolutionSupport = false;

    bool linearMask = false;
    int linearMaskAmplification = 100;
    double linearMaskSmoothness = 1.0;
    bool linearMaskInverted = true;

    bool deringing = false;
    double deringingDark = 0.1;
    double deringingBright = 0.0;

    ResidualTransfer residualTransfer = ResidualTransfer::None;
    double residualBreakpoint = 0.5;

    double dynamicRangeLow = 0.0;
    double dynamicRangeHigh = 0.0;
};

/// The engine's transform for a request: its algorithm, kernel and spacing, the detail layers and the residual, and
/// smoothings that repeat the frame's edge sample.
///
/// @param request the dialog's values.
/// @return the plan; or why the engine refuses the depth, the kernel or the spacing.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::MultiscaleTransformPlan>
planTransform(const TransformRequest& request);

/// The weight a layer is added back with: nothing for a disabled layer, one plus its bias otherwise, a positive bias
/// first multiplied - by six for the starlet, and for the multiscale linear transform by eight at the finest scale and
/// six at the others.
///
/// @param request the dialog's values.
/// @param plan the transform the request plans.
/// @param layer the layer, the residual last.
/// @return the weight.
[[nodiscard]] AFTERNIGHTPROCESSES_API double layerWeight(const TransformRequest& request,
                                                         const an::MultiscaleTransformPlan& plan, std::uint32_t layer);

/// Where a run writes the coefficients its deringing removed, one Float32 plane per channel of the image, zeroed by the
/// caller: the dark map of the negative coefficients it attenuated and the bright map of the positive ones.
struct DeringingMaps
{
    float* dark = nullptr;
    float* bright = nullptr;
    const an::ImageLayout* layout = nullptr;
};

/// Runs the transform over planar working samples in place, every channel on its own.
///
/// @param scope the run's admitted execution.
/// @param sampleType the working representation, Float32 or Float64.
/// @param samples the samples, in fractions of full scale, written in place.
/// @param layout where they sit; planar.
/// @param validity which samples mean something; a sample it rejects - or, under a rule that admits every sample, one
///        holding no number - is left as it was.
/// @param request the dialog's values.
/// @param maps where the deringing's maps go, or null.
/// @return success; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
transformInPlace(const an::ui::ExecutionScope& scope, an::SampleType sampleType, void* samples,
                 const an::ImageLayout& layout, const an::KernelValidityView& validity, const TransformRequest& request,
                 const DeringingMaps* maps);

/// Writes a preview of one layer over planar working samples in place: its coefficients times five times the bias, on a
/// grey of one half for every change and on black for the increasing or the decreasing ones, held to the unit interval.
///
/// @param scope the run's admitted execution.
/// @param sampleType the working representation.
/// @param samples the samples, overwritten.
/// @param layout where they sit; planar.
/// @param request the transform.
/// @param layer the layer, the residual last.
/// @param mode what is shown.
/// @param bias the layer's bias, which the preview scales the coefficients by.
/// @return success; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
previewLayerInPlace(const an::ui::ExecutionScope& scope, an::SampleType sampleType, void* samples,
                    const an::ImageLayout& layout, const TransformRequest& request, std::uint32_t layer,
                    LayerPreview mode, double bias);

/// Writes the linear mask over planar working samples in place, as a preview of it: each channel times the
/// amplification, held to the unit interval, inverted when asked, smoothed by the exact Gaussian of the smoothness and
/// held to the unit interval again.
///
/// @param scope the run's admitted execution.
/// @param sampleType the working representation.
/// @param samples the samples, overwritten.
/// @param layout where they sit; planar.
/// @param request the mask's amplification, smoothness and inversion.
/// @return success; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status previewLinearMaskInPlace(const an::ui::ExecutionScope& scope,
                                                                          an::SampleType sampleType, void* samples,
                                                                          const an::ImageLayout& layout,
                                                                          const TransformRequest& request);

} // namespace process_utils::multiscale
