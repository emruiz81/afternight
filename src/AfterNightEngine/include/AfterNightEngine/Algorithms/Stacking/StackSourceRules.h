// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the source path derives from a model and a contract: the stages, the calibration
/// description and one frame's share of the tile.
///
/// Constant expressions over the full `StackModel`, kept apart from
/// `Algorithms/Stacking/StackSource.h` so the pass's interface does not include the model. A caller
/// preparing calibration plans reads `stackSourceCalibrationDescriptor`, a planner or an executor
/// reads `stackSourceStagesOf` to know which stages will run before the contract is checked, and
/// `stackTileFrameBytes` is the size every band of the tile is cut at.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>

namespace an
{

/// The description a calibration plan for this stack is prepared against.
///
/// The model's input description with its representation replaced by the tile width the frames
/// are widened to, because the calibration arithmetic runs on the widened band and the plan's
/// masters are at that width. A caller hands it to the description-based
/// `prepareImageCalibration`. Should the widened description not be buildable, the input
/// description is returned unchanged.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @return the description.
[[nodiscard]] constexpr ImageDescriptor stackSourceCalibrationDescriptor(const StackModel& model) noexcept
{
    ImageDescriptorRequest request = model.request().input.descriptor.toRequest();
    request.sampleType = stackTileSampleTypeOf(request.sampleType);
    const std::optional<ImageDescriptor> descriptor = ImageDescriptor::create(request);
    return descriptor.has_value() ? *descriptor : model.request().input.descriptor;
}

/// Which stages a model and a contract imply, before either is checked.
///
/// The rule `checkStackSourceContract` applies, published so a caller can plan against it: the
/// storage is widened when it is not the tile width; raw frames are calibrated when a plan is
/// supplied; a local scope applies its surface to frames not already brought to the reference;
/// a scope with a global method applies the match. A contract that does not describe the stack
/// still yields the stages its members imply; only the check refuses it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param contract what the frames are and what brings them in.
/// @return the stages.
[[nodiscard]] constexpr StackSourceStages stackSourceStagesOf(const StackModel& model,
                                                              const StackSourceContract& contract) noexcept
{
    const StackRequest& request = model.request();
    const StackNormalizationScope scope = request.normalization.scope;
    const bool localScope =
        scope == StackNormalizationScope::Local || scope == StackNormalizationScope::LocalThenGlobal;
    StackSourceStages stages;
    stages.convert =
        request.input.descriptor.sampleType() != stackTileSampleTypeOf(request.input.descriptor.sampleType());
    stages.calibrate = contract.state == StackFrameState::Raw && !contract.calibration.empty();
    stages.normalizeLocally = localScope && contract.state != StackFrameState::LocalNormalized;
    stages.normalizeGlobally = stackNormalizationScopeUsesGlobalMethod(scope);
    stages.normalizationPrepared =
        (!stages.normalizeLocally || contract.localNormalization != nullptr) &&
        (!stages.normalizeGlobally || stages.normalizeLocally || !contract.normalization.empty());
    return stages;
}

/// The bytes one frame's band of the tile holds: the frame's width, the rows and the channels, at
/// the tile width's sample size, packed planar as `stackTileBandLayout` lays it out. The local
/// weights a tile may hold beside the bands are not included.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param rows how many rows the band holds.
/// @return the bytes.
[[nodiscard]] constexpr std::uint64_t stackTileFrameBytes(const StackModel& model, std::uint32_t rows) noexcept
{
    const ImageExtent& extent = model.request().input.descriptor.extent();
    return static_cast<std::uint64_t>(extent.width()) * rows * extent.channelCount() *
           sampleTypeSizeBytes(stackTileSampleTypeOf(model.request().input.descriptor.sampleType()));
}

} // namespace an
