// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The stars-plus-comet composite: every light split at the comet's aperture into a star layer and a comet layer, the
/// star layers stacked on the stars' registration and the comet layers on the comet's, and the comet stack blended into
/// the star stack through a feathered mask over every light's aperture - three engine operations around the ordinary
/// stack (`Algorithms/Stacking/CometComposite.h`).
///
/// **Each layer is split where its light lies.** A light's star layer is cut from its star-aligned registered image at
/// the aperture its comet position projects to through the stars' registration and the plane's origin; its comet layer
/// from its comet-aligned registered image at the aperture the comet's registration places. The sky around an aperture
/// is the median of the samples a registered image covers: an uncovered zero is stated as no data before the split
/// reads it, so an aperture near a frame's edge is not filled from a sky biased toward zero.
///
/// **The blend keeps both planes' origins.** A star-stack position is taken back through the reference light's star
/// registration with the star plane's origin and forward through its comet registration with the comet plane's
/// origin, so a canvas that is not the reference frame's own - a mosaic, an intersection, the comet's plane - reads the
/// comet stack where its comet is.
///
/// The mode is not offered to people; a run names it only through a stated setting. A drizzled composite is refused by
/// name.

#include "processes/ImageIntegration/core/CometStackingProductResolver.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/stages/StackFrameSources.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/Image.h>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace an
{
class CodecRegistry;
} // namespace an

namespace an::consumer
{
class OperationScope;
} // namespace an::consumer

namespace ImageIntegration::detail
{

/// A layer beside a composite master: `<master>.<kind>.<suffix>`.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString compositeLayerPath(const QString& finalPath, const QString& kind);

/// The fingerprint a composite's products enter its stack's fingerprint by: the comet products and the star base.
[[nodiscard]] QString
starsPlusCometProductsFingerprint(const an::ui::StarsPlusCometExperimentalProductResolveResult& products);

/// Stacks one layer's frames: the star layers when `cometLayer` is false, the comet layers when it is true. The stage
/// states the stack; the star layer's is the master's own.
using CompositeLayerStacker =
    std::function<an::Result<an::Image<float>>(stage_utils::WorkflowStackFrameSource& source, bool cometLayer)>;

/// What a composite reads.
struct StarsPlusCometCompositeRequest
{
    /// The registration database the comet run's records are read from.
    QString metricsPath;

    /// The resolved star base and comet products.
    const an::ui::StarsPlusCometExperimentalProductResolveResult* products = nullptr;

    /// The lights, in the stack's order.
    std::vector<FrameId> frames;

    /// The reference light's position among them, whose registrations the blend maps through.
    std::size_t referenceIndex = 0u;

    /// The canvas the stars' registration aligned onto.
    RegistrationCanvasMode canvasMode = RegistrationCanvasMode::Standard;

    WorkflowExecutionCounterAccumulator* counters = nullptr;
};

/// What a composite made.
struct StarsPlusCometCompositeOutput
{
    an::Image<float> composite;
    an::Image<float> cometLayer;
    an::Image<float> cometMask;
    QString starBaseDescriptorId;
    QString maskSettingsFingerprint;
    QString diagnostic;
};

/// Splits every light, stacks both layers through `stack`, draws the mask and blends.
///
/// @param scope the operation every read, split, mask and blend runs in.
/// @param registry the run's registry, or null for the engine context's.
/// @param request what the composite reads.
/// @param stack stacks a layer's frames.
/// @return the composite and its layers; or what resolving the records, reading, splitting, stacking, drawing or
///         blending refused, naming the light.
[[nodiscard]] an::Result<StarsPlusCometCompositeOutput>
buildStarsPlusCometComposite(const an::consumer::OperationScope& scope, const an::CodecRegistry* registry,
                             const StarsPlusCometCompositeRequest& request, const CompositeLayerStacker& stack);

} // namespace ImageIntegration::detail
