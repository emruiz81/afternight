// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack source path: the contract checked whole, and one band read
// through every stage it states.
//
// Three things in this file are decisions rather than mechanics.
//
// **The contract is checked whole once and per band cheaply.** `checkStackSourceContract` walks
// every plan, master, match and document record, which for a thousand frames is real work; a band
// read then checks only what it reads - this frame's plan, this frame's record and the node rows
// the band touches, this frame's row of the coefficient table - so the per-tile cost stays
// proportional to the tile.
//
// **The scalar row is included here as the authority, and the compiled row is asked for.** The
// row body is one text, compiled here inside the unnamed namespace and again per instruction set
// in the CPU source unit; the dispatch table answers which one runs, and the reference backend
// runs this one.
//
// **Every stage narrows where the operation it composes narrows.** The calibration arithmetic
// narrows to the tile width after every step because the calibration executor does, the local
// transform and the global affine compute in Float64 from the tile value and narrow once because
// the two applies do. A band read here therefore stores the bits the three materialized
// operations store in turn, which is the property the tests hold it to.

#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/stacking/StackHistory.h"
#include "kernels/reference/AlgorithmKernels.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace an
{
namespace
{

// The two shared row bodies: the surface's cell arithmetic and the source row that composes it,
// both the scalar authority in this translation unit.
#include "kernels/reference/LocalNormalizationRow.inl"
#include "kernels/reference/StackSourceRow.inl"

using stacking::failure;
using stacking::pointerIsAligned;
using stacking::refuse;

static_assert(kStackSourceGridCeiling == kMaxLocalNormalizationGridSize,
              "the source path's row scratch is sized for the largest grid the normalization domain admits");

constexpr const char* kCheckContract = "an::checkStackSourceContract";
constexpr const char* kRowsOf = "an::stackSourceRowsOf";
constexpr const char* kTileLayout = "an::stackTileBandLayout";
constexpr const char* kConvert = "an::convertStackSourceBand";
constexpr const char* kStore = "an::storeStackFrameNormalization";
constexpr const char* kAt = "an::stackFrameNormalizationAt";
constexpr const char* kBegin = "an::beginStackLocalNormalization";
constexpr const char* kFit = "an::fitStackLocalNormalizationFrame";

/// How many entries of the per-frame coefficient table one frame's channel holds: its offset,
/// then its scale.
constexpr std::uint64_t kCoefficientEntries = 2u;

/// Whether a value is a finite number.
///
/// @param value the value.
/// @return true when finite.
[[nodiscard]] bool finiteValue(double value) noexcept
{
    return std::isfinite(value);
}

/// Whether a scope applies the local surface.
///
/// @param scope the scope.
/// @return true for the two local scopes.
[[nodiscard]] constexpr bool scopeIsLocal(StackNormalizationScope scope) noexcept
{
    return scope == StackNormalizationScope::Local || scope == StackNormalizationScope::LocalThenGlobal;
}

/// The frame's plan under a contract: the shared one or its own.
///
/// @param contract the contract, whose calibration is not empty.
/// @param frameIndex which frame.
/// @return the plan.
[[nodiscard]] const CalibrationPlan& planOf(const StackSourceContract& contract, std::uint32_t frameIndex) noexcept
{
    return contract.calibration.size() == 1u ? contract.calibration[0] : contract.calibration[frameIndex];
}

/// Checks one master against the stack the plan calibrates for.
///
/// @param model the checked stack.
/// @param master the master.
/// @param role the member's spelling.
/// @param flatMayBeMono whether an RGB stack admits a Mono master here.
/// @param operation the name carried by a refusal.
/// @return success or the refusal.
[[nodiscard]] Status checkMaster(const StackModel& model, const CalibrationFrameView& master, const char* role,
                                 bool flatMayBeMono, const char* operation)
{
    const ImageDescriptor& light = model.request().input.descriptor;
    const SampleType tileType = stackTileSampleTypeOf(light.sampleType());
    if (!master.valid())
    {
        return refuse(ErrorCode::InvalidArgument, operation, role,
                      std::string(role) + " holds no frame, and the plan applies it");
    }
    const ImageDescriptor& candidate = master.descriptor();
    if (candidate.sampleType() != tileType || candidate.extent().width() != light.extent().width() ||
        candidate.extent().height() != light.extent().height() || candidate.numericDomain() != light.numericDomain() ||
        candidate.coordinateOrigin() != light.coordinateOrigin() ||
        candidate.pixelAspectRatio() != light.pixelAspectRatio())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, role,
                      std::string(role) +
                          " is not at the tile width, the frames' extent and numeric domain the stack calibrates in");
    }
    const bool sameChannels = candidate.extent().channelCount() == light.extent().channelCount() &&
                              candidate.channelSchema() == light.channelSchema() &&
                              candidate.colorSpace() == light.colorSpace() && candidate.cfa() == light.cfa();
    const bool monoFlat = flatMayBeMono && light.channelSchema() == ChannelSchema::Rgb &&
                          candidate.channelSchema() == ChannelSchema::Mono && !candidate.cfa().isMosaiced();
    if (!sameChannels && !monoFlat)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, role,
                      std::string(role) +
                          " does not match the frames' channels and CFA; only an RGB stack's flat may be Mono");
    }
    const ValidityRule rule = master.validity().descriptor.rule();
    if (rule != ValidityRule::AllValid && rule != ValidityRule::FiniteOnly)
    {
        return failure(ErrorCode::Unsupported, operation,
                       std::string(role) + " states a mask, zero or range rule, which the source path does not "
                                           "read; a master under such a rule is applied by a materialized calibration",
                       {StatusContextField::ofText(ContextKey::Parameter, role)});
    }
    if (master.validity().mask != nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, operation, role,
                      std::string(role) + " carries a mask under a rule that reads none");
    }
    return Status::success();
}

/// Checks one plan and the masters against the stack it calibrates for.
///
/// @param model the checked stack.
/// @param contract the contract, whose calibration is not empty.
/// @param frameIndex which frame's plan.
/// @param operation the name carried by a refusal.
/// @return success or the refusal.
[[nodiscard]] Status checkPlan(const StackModel& model, const StackSourceContract& contract, std::uint32_t frameIndex,
                               const char* operation)
{
    const CalibrationPlan& plan = planOf(contract, frameIndex);
    if (!plan.valid())
    {
        return refuse(ErrorCode::InvalidArgument, operation, "contract.calibration",
                      "contract.calibration holds a plan that was not prepared");
    }
    const std::optional<ImageDescriptor> described = plan.description();
    if (!described.has_value() || *described != stackSourceCalibrationDescriptor(model))
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "contract.calibration",
                      "contract.calibration holds a plan that was not prepared against the stack's calibration "
                      "description at the tile width");
    }
    if (contract.masters == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, operation, "contract.masters",
                      "contract.masters is null beside a calibration plan");
    }
    const CalibrationMasters& masters = *contract.masters;
    if (plan.usesBias() != masters.bias.has_value() || plan.usesDark() != masters.dark.has_value() ||
        plan.usesFlat() != masters.flat.has_value())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "contract.masters",
                      "contract.masters does not hold exactly the roles the plan applies");
    }
    if (masters.bias.has_value())
    {
        const Status checked = checkMaster(model, *masters.bias, "contract.masters->bias", false, operation);
        if (checked.failed())
        {
            return checked;
        }
    }
    if (masters.dark.has_value())
    {
        const Status checked = checkMaster(model, *masters.dark, "contract.masters->dark", false, operation);
        if (checked.failed())
        {
            return checked;
        }
    }
    if (masters.flat.has_value())
    {
        const Status checked = checkMaster(model, *masters.flat, "contract.masters->flat", true, operation);
        if (checked.failed())
        {
            return checked;
        }
        if (plan.flatChannelCount() != masters.flat->descriptor().extent().channelCount())
        {
            return refuse(ErrorCode::IncompatibleArguments, operation, "contract.masters->flat",
                          "contract.masters->flat does not have the channels the plan was prepared with");
        }
    }
    return Status::success();
}

/// Checks one global match against the model, and the identity on the reference frame.
///
/// @param model the checked stack.
/// @param match the match.
/// @param frameIndex which frame.
/// @param parameter the member's spelling.
/// @param operation the name carried by a refusal.
/// @return success or the refusal.
[[nodiscard]] Status checkMatch(const StackModel& model, const FrameNormalization& match, std::uint32_t frameIndex,
                                const char* parameter, const char* operation)
{
    const StackRequest& request = model.request();
    const std::uint32_t channels = request.input.descriptor.extent().channelCount();
    if (!request.normalization.globalMethod.has_value())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, parameter,
                      std::string(parameter) + " carries a global match for a stack whose scope applies none");
    }
    if (match.method != *request.normalization.globalMethod)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, parameter,
                      std::string(parameter) + " was matched by a method that is not the stack's global method");
    }
    if (match.estimator != request.normalization.scaleEstimator)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, parameter,
                      std::string(parameter) + " was derived with an estimator that is not the stack's");
    }
    if (match.channelCount != channels)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, parameter,
                      std::string(parameter) + " does not carry one coefficient pair per channel of the frames");
    }
    const bool multiplicative = normalizationMethodIsMultiplicative(match.method);
    const bool reference = frameIndex == request.normalization.referenceFrame;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const NormalizationChannelCoefficients& coefficients = match.channels[channel];
        if (!finiteValue(coefficients.scale) || coefficients.scale == 0.0 || !finiteValue(coefficients.offset))
        {
            return failure(ErrorCode::InvalidArgument, operation,
                           std::string(parameter) + " carries a non-finite coefficient or a zero scale",
                           {StatusContextField::ofText(ContextKey::Parameter, parameter),
                            StatusContextField::ofCount(ContextKey::Requested, frameIndex)});
        }
        if (multiplicative && coefficients.offset != 0.0)
        {
            return failure(ErrorCode::InvalidArgument, operation,
                           std::string(parameter) + " carries an offset under a multiplicative method",
                           {StatusContextField::ofText(ContextKey::Parameter, parameter),
                            StatusContextField::ofCount(ContextKey::Requested, frameIndex)});
        }
        if (reference && (coefficients.scale != 1.0 || coefficients.offset != 0.0))
        {
            return failure(ErrorCode::IncompatibleArguments, operation,
                           std::string(parameter) + " matches the reference frame by something other than the identity",
                           {StatusContextField::ofText(ContextKey::Parameter, parameter),
                            StatusContextField::ofCount(ContextKey::Requested, frameIndex)});
        }
    }
    return Status::success();
}

/// Whether every value of a grid's node rows for every channel is finite.
///
/// @param values the grid.
/// @param shape its geometry.
/// @param firstNodeRow the first node row to check.
/// @param lastNodeRow the last node row to check, inclusive.
/// @return true when every checked node is finite.
[[nodiscard]] bool gridRowsFinite(std::span<const double> values, const LocalNormalizationGridShape& shape,
                                  std::uint32_t firstNodeRow, std::uint32_t lastNodeRow) noexcept
{
    const std::uint32_t nodes = shape.nodesPerAxis();
    for (std::uint32_t channel = 0u; channel < shape.channelCount; ++channel)
    {
        for (std::uint32_t nodeY = firstNodeRow; nodeY <= lastNodeRow; ++nodeY)
        {
            const double* row = values.data() + localNormalizationNodeIndex(shape, channel, 0u, nodeY);
            for (std::uint32_t nodeX = 0u; nodeX < nodes; ++nodeX)
            {
                if (!finiteValue(row[nodeX]))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

/// Checks the document's geometry against the stack and its reference against the model.
///
/// @param model the checked stack.
/// @param document the document.
/// @param operation the name carried by a refusal.
/// @return success or the refusal.
[[nodiscard]] Status checkDocumentShape(const StackModel& model, const LocalNormalizationDocument& document,
                                        const char* operation)
{
    const StackRequest& request = model.request();
    const ImageExtent& extent = request.input.descriptor.extent();
    const Status shape = checkLocalNormalizationGridShape(document.shape);
    if (shape.failed())
    {
        return shape;
    }
    if (document.shape.width != extent.width() || document.shape.height != extent.height() ||
        document.shape.channelCount != extent.channelCount())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "contract.localNormalization",
                      "contract.localNormalization was fitted on another extent or channel count than the frames");
    }
    if (!isLocalGridInterpolation(document.request.interpolation))
    {
        return refuse(ErrorCode::InvalidArgument, operation, "contract.localNormalization",
                      "contract.localNormalization names an interpolation this build does not define");
    }
    if (document.referenceBackground.size() != document.shape.valueCount())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "contract.localNormalization",
                      "contract.localNormalization holds a reference grid that is not the shape's size");
    }
    if (document.frames.size() != request.input.frameCount)
    {
        return failure(ErrorCode::IncompatibleArguments, operation,
                       "contract.localNormalization does not hold one record per frame of the stack",
                       {StatusContextField::ofText(ContextKey::Parameter, "contract.localNormalization"),
                        StatusContextField::ofCount(ContextKey::Requested, document.frames.size()),
                        StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount)});
    }
    if (document.referenceIndex.has_value() && *document.referenceIndex != request.normalization.referenceFrame)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "contract.localNormalization",
                      "contract.localNormalization names a reference frame that is not the stack's");
    }
    return Status::success();
}

/// Checks one frame's record of the document, and the node rows a band of it reads.
///
/// @param model the checked stack.
/// @param document the document.
/// @param frameIndex which frame.
/// @param firstNodeRow the first node row the band reads.
/// @param lastNodeRow the last node row the band reads, inclusive.
/// @param operation the name carried by a refusal.
/// @return success or the refusal.
[[nodiscard]] Status checkDocumentFrame(const StackModel& model, const LocalNormalizationDocument& document,
                                        std::uint32_t frameIndex, std::uint32_t firstNodeRow, std::uint32_t lastNodeRow,
                                        const char* operation)
{
    const StackRequest& request = model.request();
    const LocalNormalizationFrameCoefficients& record = document.frames[frameIndex];
    const std::uint64_t valueCount = document.shape.valueCount();
    const auto frameFailure = [&](ErrorCode code, const char* message) {
        return failure(code, operation, message,
                       {StatusContextField::ofText(ContextKey::Parameter, "contract.localNormalization"),
                        StatusContextField::ofCount(ContextKey::Requested, frameIndex)});
    };
    if (record.frameIndex != frameIndex)
    {
        return frameFailure(ErrorCode::IncompatibleArguments,
                            "contract.localNormalization holds a record out of its frame's place");
    }
    if (record.background.size() != valueCount)
    {
        return frameFailure(ErrorCode::IncompatibleArguments,
                            "contract.localNormalization holds a background grid that is not the shape's size");
    }
    if (!record.scaleGrid.empty() && record.scaleGrid.size() != valueCount)
    {
        return frameFailure(ErrorCode::IncompatibleArguments,
                            "contract.localNormalization holds a scale grid that is not the shape's size");
    }
    for (std::uint32_t channel = 0u; channel < document.shape.channelCount; ++channel)
    {
        if (!finiteValue(record.scale[channel]) || record.scale[channel] <= 0.0)
        {
            return frameFailure(ErrorCode::InvalidArgument,
                                "contract.localNormalization holds a scale that is not positive and finite");
        }
    }
    if (!gridRowsFinite(document.referenceBackground, document.shape, firstNodeRow, lastNodeRow) ||
        !gridRowsFinite(record.background, document.shape, firstNodeRow, lastNodeRow) ||
        (!record.scaleGrid.empty() && !gridRowsFinite(record.scaleGrid, document.shape, firstNodeRow, lastNodeRow)))
    {
        return frameFailure(ErrorCode::InvalidArgument, "contract.localNormalization holds a non-finite node");
    }
    const bool postLocal = request.normalization.scope == StackNormalizationScope::LocalThenGlobal;
    if (record.global.has_value() != postLocal)
    {
        return frameFailure(ErrorCode::IncompatibleArguments,
                            postLocal
                                ? "contract.localNormalization lacks the post-local global match the stack applies"
                                : "contract.localNormalization carries a post-local global match the stack "
                                  "does not apply");
    }
    if (record.global.has_value())
    {
        return checkMatch(model, *record.global, frameIndex, "contract.localNormalization", operation);
    }
    return Status::success();
}

/// The node rows a band of frame rows reads: one before the first row's cell to two after the
/// last row's, clamped to the grid.
///
/// @param document the document.
/// @param firstRow the band's first frame row.
/// @param lastRow the band's last frame row, inclusive.
/// @param firstNodeRow receives the first node row.
/// @param lastNodeRow receives the last node row, inclusive.
void nodeRowsOf(const LocalNormalizationDocument& document, std::uint32_t firstRow, std::uint32_t lastRow,
                std::uint32_t& firstNodeRow, std::uint32_t& lastNodeRow) noexcept
{
    double fraction = 0.0;
    const std::uint32_t gridSize = document.shape.gridSize;
    const std::uint32_t firstCell =
        localNormalizationCellOfCoordinate(static_cast<double>(firstRow), document.shape.height, gridSize, fraction);
    const std::uint32_t lastCell =
        localNormalizationCellOfCoordinate(static_cast<double>(lastRow), document.shape.height, gridSize, fraction);
    firstNodeRow = firstCell > 0u ? firstCell - 1u : 0u;
    lastNodeRow = lastCell + 2u < gridSize ? lastCell + 2u : gridSize;
}

/// Checks the members of a contract that every band reads, before any band is read.
///
/// @param model the checked stack.
/// @param contract the contract.
/// @return the stages or the first refusal.
[[nodiscard]] Result<StackSourceStages> checkContract(const StackModel& model, const StackSourceContract& contract)
{
    const StackRequest& request = model.request();
    if (!isStackFrameState(contract.state))
    {
        return refuse(ErrorCode::InvalidArgument, kCheckContract, "contract.state",
                      "contract.state is not a state this build defines");
    }
    const StackSourceStages stages = stackSourceStagesOf(model, contract);

    if (!contract.calibration.empty())
    {
        if (contract.state != StackFrameState::Raw)
        {
            return refuse(ErrorCode::IncompatibleArguments, kCheckContract, "contract.calibration",
                          "contract.calibration was supplied for frames that are not raw, and a calibrated frame is "
                          "not calibrated again");
        }
        if (contract.calibration.size() != 1u && contract.calibration.size() != request.input.frameCount)
        {
            return failure(ErrorCode::IncompatibleArguments, kCheckContract,
                           "contract.calibration holds neither one plan for every frame nor one per frame",
                           {StatusContextField::ofText(ContextKey::Parameter, "contract.calibration"),
                            StatusContextField::ofCount(ContextKey::Requested, contract.calibration.size()),
                            StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount)});
        }
        for (std::uint32_t frame = 0u; frame < request.input.frameCount; ++frame)
        {
            const Status checked = checkPlan(model, contract, frame, kCheckContract);
            if (checked.failed())
            {
                return checked;
            }
            if (contract.calibration.size() == 1u)
            {
                break;
            }
        }
    }
    else if (contract.masters != nullptr)
    {
        return refuse(ErrorCode::IncompatibleArguments, kCheckContract, "contract.masters",
                      "contract.masters were supplied without a plan that applies them");
    }

    if (contract.localNormalization != nullptr)
    {
        if (!stages.normalizeLocally)
        {
            return refuse(ErrorCode::IncompatibleArguments, kCheckContract, "contract.localNormalization",
                          contract.state == StackFrameState::LocalNormalized
                              ? "contract.localNormalization was supplied for frames already brought to the reference"
                              : "contract.localNormalization was supplied for a stack whose scope applies no local "
                                "surface");
        }
        const Status shape = checkDocumentShape(model, *contract.localNormalization, kCheckContract);
        if (shape.failed())
        {
            return shape;
        }
        for (std::uint32_t frame = 0u; frame < request.input.frameCount; ++frame)
        {
            const Status checked = checkDocumentFrame(model, *contract.localNormalization, frame, 0u,
                                                      contract.localNormalization->shape.gridSize, kCheckContract);
            if (checked.failed())
            {
                return checked;
            }
        }
    }

    if (!contract.normalization.empty())
    {
        if (!stages.normalizeGlobally)
        {
            return refuse(ErrorCode::IncompatibleArguments, kCheckContract, "contract.normalization",
                          "contract.normalization was supplied for a stack whose scope applies no global match");
        }
        if (stages.normalizeLocally)
        {
            return refuse(ErrorCode::IncompatibleArguments, kCheckContract, "contract.normalization",
                          "contract.normalization was supplied beside a local stage, whose document carries the "
                          "post-local matches");
        }
        if (contract.normalization.size() != request.input.frameCount)
        {
            return failure(ErrorCode::IncompatibleArguments, kCheckContract,
                           "contract.normalization does not hold one match per frame of the stack",
                           {StatusContextField::ofText(ContextKey::Parameter, "contract.normalization"),
                            StatusContextField::ofCount(ContextKey::Requested, contract.normalization.size()),
                            StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount)});
        }
        for (std::uint32_t frame = 0u; frame < request.input.frameCount; ++frame)
        {
            const Status checked =
                checkMatch(model, contract.normalization[frame], frame, "contract.normalization", kCheckContract);
            if (checked.failed())
            {
                return checked;
            }
        }
    }
    return stages;
}

/// A band checked against the model: its shape and how its mask and tile are laid out.
struct BandGeometry
{
    /// The band's width, which is the frames'.
    std::uint32_t width = 0u;
    /// The band's rows.
    std::uint32_t rows = 0u;
    /// The band's channels, which are the frames'.
    std::uint32_t channels = 0u;
    /// The whole frame's height.
    std::uint32_t height = 0u;
    /// The source mask's bit-planes: zero under a rule that reads no mask, else one shared or one
    /// per channel.
    std::uint32_t maskPlanes = 0u;
    /// Whether the tile band is the source band, written where it was read.
    bool inPlace = false;
};

/// Whether two byte ranges share a byte.
///
/// @param left one range's first byte.
/// @param leftBytes its length.
/// @param right the other range's first byte.
/// @param rightBytes its length.
/// @return true when they overlap.
[[nodiscard]] bool rangesOverlap(const void* left, std::uint64_t leftBytes, const void* right,
                                 std::uint64_t rightBytes) noexcept
{
    const std::uintptr_t leftBegin = reinterpret_cast<std::uintptr_t>(left);
    const std::uintptr_t rightBegin = reinterpret_cast<std::uintptr_t>(right);
    return leftBegin < rightBegin + static_cast<std::uintptr_t>(rightBytes) &&
           rightBegin < leftBegin + static_cast<std::uintptr_t>(leftBytes);
}

/// Checks the source band and the tile band against the model.
///
/// @param model the checked stack.
/// @param source the band.
/// @param tile where it goes.
/// @return the geometry or the first refusal.
[[nodiscard]] Result<BandGeometry> checkBands(const StackModel& model, const StackSourceBand& source,
                                              const StackTileBand& tile)
{
    const StackRequest& request = model.request();
    const ImageDescriptor& descriptor = request.input.descriptor;
    const ImageExtent& frame = descriptor.extent();
    const SampleType tileType = stackTileSampleTypeOf(descriptor.sampleType());
    if (source.samples == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "source.samples", "source.samples is null");
    }
    if (source.layout == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "source.layout", "source.layout is null");
    }
    if (source.sampleType != descriptor.sampleType())
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "source.sampleType",
                      "source.sampleType is not the representation the stack's input is described as");
    }
    const ImageLayout& layout = *source.layout;
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(source.sampleType))
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "source.layout",
                      "source.layout does not describe elements of the stored representation");
    }
    if (!pointerIsAligned(source.samples, layout.elementSizeBytes()))
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "source.samples",
                      "source.samples do not begin on their element boundary");
    }
    const ImageExtent& extent = layout.extent();
    if (extent.width() != frame.width() || extent.channelCount() != frame.channelCount())
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "source.layout",
                      "source.layout does not have the frames' width or channel count");
    }
    if (static_cast<std::uint64_t>(layout.planeCount()) * layout.samplesPerPixel() != extent.channelCount())
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "source.layout",
                      "source.layout's planes and components do not address every channel exactly once");
    }
    if (source.frameIndex >= request.input.frameCount)
    {
        return failure(ErrorCode::OutOfRange, kConvert, "source.frameIndex is not below the stack's frame count",
                       {StatusContextField::ofText(ContextKey::Parameter, "source.frameIndex"),
                        StatusContextField::ofCount(ContextKey::Requested, source.frameIndex),
                        StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount)});
    }
    if (source.firstRow >= frame.height() ||
        static_cast<std::uint64_t>(source.firstRow) + extent.height() > frame.height())
    {
        return failure(ErrorCode::OutOfRange, kConvert,
                       "source.firstRow and the band's rows do not lie inside the frame",
                       {StatusContextField::ofText(ContextKey::Parameter, "source.firstRow"),
                        StatusContextField::ofCount(ContextKey::Requested,
                                                    static_cast<std::uint64_t>(source.firstRow) + extent.height()),
                        StatusContextField::ofCount(ContextKey::Limit, frame.height())});
    }

    BandGeometry geometry;
    geometry.width = extent.width();
    geometry.rows = extent.height();
    geometry.channels = extent.channelCount();
    geometry.height = frame.height();

    const ValidityDescriptor& rule = source.validity.descriptor;
    if (!isValidityRule(rule.rule()))
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "source.validity",
                      "source.validity states a rule this build does not define");
    }
    if (!rule.needsMask())
    {
        if (source.validity.mask != nullptr)
        {
            return refuse(ErrorCode::InvalidArgument, kConvert, "source.validity.mask",
                          "source.validity.mask was supplied under a validity rule that reads no packed mask");
        }
    }
    else
    {
        if (source.validity.mask == nullptr)
        {
            return refuse(ErrorCode::InvalidArgument, kConvert, "source.validity.mask",
                          "source.validity.mask is required by the explicit-mask rule and none was given");
        }
        const ValidityMaskView<const std::uint8_t>& mask = *source.validity.mask;
        if (mask.width() != geometry.width || mask.height() != geometry.rows ||
            (mask.planeCount() != 1u && mask.planeCount() != geometry.channels))
        {
            return refuse(ErrorCode::IncompatibleArguments, kConvert, "source.validity.mask",
                          "source.validity.mask does not cover the band's extent with one shared bit-plane or one "
                          "per channel");
        }
        geometry.maskPlanes = mask.planeCount();
    }
    if (rule.rule() == ValidityRule::DomainRange &&
        (!rule.lowestValid().has_value() || !rule.highestValid().has_value()))
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "source.validity",
                      "source.validity states a range rule without its bounds");
    }

    if (tile.samples == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "tile.samples", "tile.samples is null");
    }
    if (tile.layout == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "tile.layout", "tile.layout is null");
    }
    if (tile.sampleType != tileType)
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "tile.sampleType",
                      "tile.sampleType is not the tile width the plan states for this stack");
    }
    const ImageLayout& tileLayout = *tile.layout;
    if (tileLayout.elementSizeBytes() != sampleTypeSizeBytes(tileType))
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "tile.layout",
                      "tile.layout does not describe elements of the tile width");
    }
    if (!pointerIsAligned(tile.samples, tileLayout.elementSizeBytes()))
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "tile.samples",
                      "tile.samples do not begin on their element boundary");
    }
    if (tileLayout.extent() != extent)
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "tile.layout",
                      "tile.layout does not describe the band's width, rows and channels");
    }
    if (static_cast<std::uint64_t>(tileLayout.planeCount()) * tileLayout.samplesPerPixel() != extent.channelCount())
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "tile.layout",
                      "tile.layout's planes and components do not address every channel exactly once");
    }
    geometry.inPlace = tile.samples == source.samples;
    if (geometry.inPlace)
    {
        if (source.sampleType != tileType || tileLayout.samplesPerPixel() != layout.samplesPerPixel() ||
            tileLayout.rowStrideBytes() != layout.rowStrideBytes() ||
            tileLayout.planeStrideBytes() != layout.planeStrideBytes())
        {
            return failure(ErrorCode::Aliasing, kConvert,
                           "a band is written where it was read only at the tile width and through the same layout",
                           {StatusContextField::ofText(ContextKey::Parameter, "tile.samples")});
        }
    }
    else if (rangesOverlap(source.samples, layout.totalBytes(), tile.samples, tileLayout.totalBytes()))
    {
        return failure(ErrorCode::Aliasing, kConvert,
                       "the tile band partly overlaps the source band; write the band where it was read or into "
                       "disjoint storage",
                       {StatusContextField::ofText(ContextKey::Parameter, "tile.samples")});
    }
    return geometry;
}

/// Where one sample of a borrowed frame sits.
///
/// @tparam SampleT the representation.
/// @param samples the frame's first byte.
/// @param layout where its samples sit.
/// @param x the column.
/// @param y the row within the layout.
/// @param channel the channel.
/// @return the sample's address.
template <typename SampleT>
[[nodiscard]] const SampleT* sampleAddress(const void* samples, const ImageLayout& layout, std::uint32_t x,
                                           std::uint32_t y, std::uint32_t channel) noexcept
{
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t inPixel = channel % layout.samplesPerPixel();
    const auto* base = static_cast<const std::byte*>(samples);
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                 (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + inPixel) * sizeof(SampleT);
    return reinterpret_cast<const SampleT*>(base + offset);
}

/// Where one sample of writable storage sits.
///
/// @tparam SampleT the representation.
/// @param samples the storage's first byte.
/// @param layout where its samples sit.
/// @param x the column.
/// @param y the row within the layout.
/// @param channel the channel.
/// @return the sample's address.
template <typename SampleT>
[[nodiscard]] SampleT* sampleAddress(void* samples, const ImageLayout& layout, std::uint32_t x, std::uint32_t y,
                                     std::uint32_t channel) noexcept
{
    return const_cast<SampleT*>(sampleAddress<SampleT>(static_cast<const void*>(samples), layout, x, y, channel));
}

/// The compiled row for one stored representation, or null.
///
/// @tparam SourceT the stored representation.
/// @param runs the selected table's rows, or null.
/// @return the row.
template <typename SourceT> [[nodiscard]] auto compiledRunOf(const StackSourceCpuRuns* runs) noexcept
{
    using DestT = std::conditional_t<std::is_same_v<SourceT, double>, double, float>;
    StackSourceRunFn<SourceT, DestT> run = nullptr;
    if (runs != nullptr)
    {
        if constexpr (std::is_same_v<SourceT, std::uint8_t>)
        {
            run = runs->uint8;
        }
        else if constexpr (std::is_same_v<SourceT, std::int8_t>)
        {
            run = runs->int8;
        }
        else if constexpr (std::is_same_v<SourceT, std::uint16_t>)
        {
            run = runs->uint16;
        }
        else if constexpr (std::is_same_v<SourceT, std::int16_t>)
        {
            run = runs->int16;
        }
        else if constexpr (std::is_same_v<SourceT, std::uint32_t>)
        {
            run = runs->uint32;
        }
        else if constexpr (std::is_same_v<SourceT, std::int32_t>)
        {
            run = runs->int32;
        }
        else if constexpr (std::is_same_v<SourceT, float>)
        {
            run = runs->float32;
        }
        else
        {
            run = runs->float64;
        }
    }
    return run;
}

/// Everything a band read holds constant across its rows.
struct BandRun
{
    /// The checked stack.
    const StackModel* model = nullptr;
    /// The contract the stages come from.
    const StackSourceContract* contract = nullptr;
    /// The band read from the source.
    const StackSourceBand* source = nullptr;
    /// Where the converted band is written.
    const StackTileBand* tile = nullptr;
    /// The two bands' checked geometry.
    BandGeometry geometry{};
    /// Which stages the band passes through.
    StackSourceStages stages{};
    /// How a stored sample is widened to the tile width, from the frames' numeric domain.
    ConversionPolicy policy{};
    /// The rule the tile carries: finite-only after calibration or a range rule, else the
    /// source's.
    ValidityDescriptor outputValidity{};
    /// This frame's calibration plan, under the calibration stage.
    const CalibrationPlan* plan = nullptr;
    /// The CFA block's side for a mosaiced frame, which selects the flat's per-site factors; zero
    /// otherwise.
    std::uint32_t cfaSide = 0u;
    /// This frame's record of the local-normalization document, under the local stage.
    const LocalNormalizationFrameCoefficients* record = nullptr;
    /// Whether the local surface transforms this frame; the document's reference frame is not
    /// transformed by it.
    bool applyLocal = false;
    /// The coefficient table, under the global stage.
    std::span<const double> table{};
    /// Every worker's slice, whose tail holds the local surface's row scratch.
    std::span<const std::span<std::byte>> workerScratch{};
    /// The bytes of that tail, `stackSourceScratchBytesPerWorker` of the model.
    std::uint64_t sourceScratchBytes = 0u;
    /// Whether the global method scales without an offset.
    bool multiplicative = false;
};

/// The counts every row adds to.
struct BandCounters
{
    std::atomic<std::uint64_t> written{0u};
    std::atomic<std::uint64_t> transformed{0u};
    std::atomic<std::uint64_t> preserved{0u};
    std::atomic<std::uint64_t> invalidInputs{0u};
    std::atomic<std::uint64_t> nonFinite{0u};
    std::atomic<std::uint64_t> collisions{0u};
    std::atomic<bool> finite{true};
};

/// The calibration row of one channel and one frame row.
///
/// @tparam DestT the tile representation.
/// @param run the band.
/// @param frameRow the frame row.
/// @param channel the channel.
/// @return the masters' rows and the plan's scalars.
template <typename DestT>
[[nodiscard]] StackSourceCalibrationRow calibrationRowOf(const BandRun& run, std::uint32_t frameRow,
                                                         std::uint32_t channel) noexcept
{
    StackSourceCalibrationRow row;
    const CalibrationPlan& plan = *run.plan;
    const CalibrationMasters& masters = *run.contract->masters;
    row.pedestal = plan.pedestal();
    row.darkScale = plan.effectiveDarkScale();
    if (masters.bias.has_value())
    {
        row.bias = sampleAddress<DestT>(masters.bias->samples(), masters.bias->layout(), 0u, frameRow, channel);
        row.biasStride = masters.bias->layout().samplesPerPixel();
    }
    if (masters.dark.has_value())
    {
        row.dark = sampleAddress<DestT>(masters.dark->samples(), masters.dark->layout(), 0u, frameRow, channel);
        row.darkStride = masters.dark->layout().samplesPerPixel();
    }
    if (masters.flat.has_value())
    {
        const std::uint32_t flatChannel = plan.flatChannelCount() == 1u ? 0u : channel;
        row.flat = sampleAddress<DestT>(masters.flat->samples(), masters.flat->layout(), 0u, frameRow, flatChannel);
        row.flatStride = masters.flat->layout().samplesPerPixel();
        if (plan.flatNormalization() == FlatNormalizationMode::CentralMean)
        {
            const std::span<const double> factors = plan.flatNormalizationFactors();
            if (run.cfaSide != 0u)
            {
                row.flatFactors = factors.data() + (frameRow % run.cfaSide) * run.cfaSide;
                row.flatSites = run.cfaSide;
            }
            else
            {
                const std::uint32_t group = plan.flatChannelCount() == run.geometry.channels ? flatChannel : 0u;
                row.flatFactors = factors.data() + group;
                row.flatSites = 1u;
            }
        }
    }
    return row;
}

/// Reads every row of the band through one row function.
///
/// @tparam SourceT the stored representation.
/// @tparam DestT the tile representation.
/// @param execution the operation.
/// @param run the band.
/// @param compiled the compiled row, or null for the scalar authority.
/// @param counters receive what the rows did.
/// @return success or the loop's failure.
template <typename SourceT, typename DestT>
[[nodiscard]] Status runRows(const ExecutionContext& execution, const BandRun& run,
                             StackSourceRunFn<SourceT, DestT> compiled, BandCounters& counters)
{
    const BandGeometry& geometry = run.geometry;
    const StackSourceBand& source = *run.source;
    const StackTileBand& tile = *run.tile;
    const std::uint64_t taskCount = static_cast<std::uint64_t>(geometry.rows) * geometry.channels;
    const TaskCost cost{.bytesPerItem =
                            static_cast<std::uint64_t>(geometry.width) * (sizeof(SourceT) + 4u * sizeof(DestT)),
                        .operationsPerItem = static_cast<std::uint64_t>(geometry.width) * 32u};
    const StackSourceRunFn<SourceT, DestT> row = compiled != nullptr ? compiled : &stackSourceRunOf<SourceT, DestT>;
    const ValidityRule outputRule = run.outputValidity.rule();
    const LocalNormalizationDocument* document = run.contract->localNormalization;
    const std::uint32_t nodes = run.applyLocal ? document->shape.nodesPerAxis() : 0u;
    const std::uint64_t mixedDoubles = run.applyLocal ? localNormalizationRowScratchDoubles(nodes) : 0u;

    return parallelForWorkers(execution, taskCount, cost, [&](WorkerTask task) {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        LocalNormalizationRowScratch scratch{};
        double* fractions = nullptr;
        std::uint32_t* cellStarts = nullptr;
        // The local surface's row scratch is the last `sourceScratchBytes` of the worker's slice,
        // where the per-worker contract places it whatever the strategy.
        if (run.applyLocal)
        {
            const std::span<std::byte> slice = run.workerScratch[task.workerSlot];
            auto* block = reinterpret_cast<double*>(slice.data() + (slice.size() - run.sourceScratchBytes));
            scratch = localNormalizationRowScratchOver(block, nodes);
            fractions = block + mixedDoubles;
            cellStarts = reinterpret_cast<std::uint32_t*>(fractions + geometry.width);
            localNormalizationPrepareColumns(geometry.width, document->shape.gridSize, fractions, cellStarts);
        }
        std::uint64_t localWritten = 0u;
        std::uint64_t localTransformed = 0u;
        std::uint64_t localPreserved = 0u;
        std::uint64_t localInvalid = 0u;
        std::uint64_t localNonFinite = 0u;
        std::uint64_t localCollisions = 0u;
        bool localFinite = true;
        for (std::uint64_t item = task.range.begin; item < task.range.end; ++item)
        {
            const auto channel = static_cast<std::uint32_t>(item / geometry.rows);
            const auto y = static_cast<std::uint32_t>(item % geometry.rows);
            const std::uint32_t frameRow = source.firstRow + y;

            StackSourceRunParameters parameters;
            parameters.sourceStride = source.layout->samplesPerPixel();
            parameters.destinationStride = tile.layout->samplesPerPixel();
            parameters.rule = source.validity.descriptor.rule();
            parameters.lowestValid = source.validity.descriptor.lowestValid().value_or(0.0);
            parameters.highestValid = source.validity.descriptor.highestValid().value_or(0.0);
            if (geometry.maskPlanes != 0u)
            {
                const std::uint32_t plane = geometry.maskPlanes == 1u ? 0u : channel;
                const std::optional<std::span<const std::uint8_t>> bits = source.validity.mask->row(plane, y);
                parameters.maskBits = bits.has_value() ? bits->data() : nullptr;
                parameters.maskBitOffset = 0u;
            }
            parameters.policy = run.policy;
            parameters.calibrate = run.stages.calibrate;
            if (run.stages.calibrate)
            {
                parameters.calibration = calibrationRowOf<DestT>(run, frameRow, channel);
            }
            LocalNormalizationRunParameters local{};
            if (run.applyLocal)
            {
                const LocalNormalizationGridShape& shape = document->shape;
                const std::uint64_t planeOffset = static_cast<std::uint64_t>(channel) * shape.nodeCount();
                local = localNormalizationPrepareRow(
                    document->referenceBackground.data() + planeOffset, run.record->background.data() + planeOffset,
                    run.record->scaleGrid.empty() ? nullptr : run.record->scaleGrid.data() + planeOffset,
                    shape.nodesPerAxis(), shape.gridSize, shape.height, frameRow,
                    document->request.interpolation == LocalGridInterpolation::Bicubic, run.record->scale[channel],
                    fractions, cellStarts, 1u, outputRule, scratch);
                parameters.local = &local;
            }
            parameters.normalizeGlobally = run.stages.normalizeGlobally;
            if (run.stages.normalizeGlobally)
            {
                const std::uint64_t entry =
                    (static_cast<std::uint64_t>(source.frameIndex) * geometry.channels + channel) * kCoefficientEntries;
                parameters.offset = run.table[entry];
                parameters.scale = run.table[entry + 1u];
                parameters.multiplicative = run.multiplicative;
            }
            parameters.zeroRule = outputRule == ValidityRule::ZeroIsNoData;

            const SourceT* sourceRow = sampleAddress<SourceT>(source.samples, *source.layout, 0u, y, channel);
            DestT* destinationRow = sampleAddress<DestT>(tile.samples, *tile.layout, 0u, y, channel);
            const StackSourceRunOutcome outcome = row(sourceRow, destinationRow, geometry.width, parameters);
            localWritten += outcome.written;
            localTransformed += outcome.transformed;
            localPreserved += outcome.preserved;
            localInvalid += outcome.invalidCalibrationInputs;
            localNonFinite += outcome.nonFiniteOutputs;
            localCollisions += outcome.noDataCollisions;
            localFinite = localFinite && outcome.finite;
        }
        counters.written.fetch_add(localWritten, std::memory_order_relaxed);
        counters.transformed.fetch_add(localTransformed, std::memory_order_relaxed);
        counters.preserved.fetch_add(localPreserved, std::memory_order_relaxed);
        counters.invalidInputs.fetch_add(localInvalid, std::memory_order_relaxed);
        counters.nonFinite.fetch_add(localNonFinite, std::memory_order_relaxed);
        counters.collisions.fetch_add(localCollisions, std::memory_order_relaxed);
        if (!localFinite)
        {
            counters.finite.store(false, std::memory_order_relaxed);
        }
        return Status::success();
    });
}

/// Reads the band through the row of its stored representation.
///
/// @param execution the operation.
/// @param run the band.
/// @param runs the selected table's rows, or null for the scalar authority.
/// @param counters receive what the rows did.
/// @param compiledUsed receives whether a compiled row ran.
/// @return success or the loop's failure.
[[nodiscard]] Status runBand(const ExecutionContext& execution, const BandRun& run, const StackSourceCpuRuns* runs,
                             BandCounters& counters, bool& compiledUsed)
{
    const auto dispatch = [&]<typename SourceT>() {
        using DestT = std::conditional_t<std::is_same_v<SourceT, double>, double, float>;
        const StackSourceRunFn<SourceT, DestT> compiled = compiledRunOf<SourceT>(runs);
        compiledUsed = compiled != nullptr;
        return runRows<SourceT, DestT>(execution, run, compiled, counters);
    };
    switch (run.source->sampleType)
    {
    case SampleType::UInt8:
        return dispatch.template operator()<std::uint8_t>();
    case SampleType::Int8:
        return dispatch.template operator()<std::int8_t>();
    case SampleType::UInt16:
        return dispatch.template operator()<std::uint16_t>();
    case SampleType::Int16:
        return dispatch.template operator()<std::int16_t>();
    case SampleType::UInt32:
        return dispatch.template operator()<std::uint32_t>();
    case SampleType::Int32:
        return dispatch.template operator()<std::int32_t>();
    case SampleType::Float32:
        return dispatch.template operator()<float>();
    case SampleType::Float64:
        return dispatch.template operator()<double>();
    }
    return refuse(ErrorCode::InvalidArgument, kConvert, "source.sampleType",
                  "source.sampleType is not one of the resident representations");
}

/// The table's entry count for a model, or nothing when the scope names no global method.
///
/// @param model the checked stack.
/// @param operation the name carried by a refusal.
/// @return the count or the refusal.
[[nodiscard]] Result<std::uint64_t> tableEntriesOf(const StackModel& model, const char* operation)
{
    const StackRequest& request = model.request();
    if (!stackNormalizationScopeUsesGlobalMethod(request.normalization.scope))
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "model",
                      "model applies no global match, so the stack keeps no coefficient table");
    }
    return static_cast<std::uint64_t>(request.input.frameCount) * request.input.descriptor.extent().channelCount() *
           kCoefficientEntries;
}

/// Checks a frame view for the fits: the stack's geometry at the tile width.
///
/// @param model the checked stack.
/// @param frame the view.
/// @param parameter the member's spelling.
/// @param operation the name carried by a refusal.
/// @return success or the refusal.
[[nodiscard]] Status checkFitFrame(const StackModel& model, const NormalizationFrameView& frame, const char* parameter,
                                   const char* operation)
{
    const ImageDescriptor& descriptor = model.request().input.descriptor;
    if (!frame.valid())
    {
        return refuse(ErrorCode::InvalidArgument, operation, parameter, std::string(parameter) + " holds no frame");
    }
    if (frame.descriptor().sampleType() != stackTileSampleTypeOf(descriptor.sampleType()) ||
        frame.layout().extent() != descriptor.extent())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, parameter,
                      std::string(parameter) + " is not the stack's extent at the tile width");
    }
    return Status::success();
}

} // namespace

const char* stackFrameStateName(StackFrameState state) noexcept
{
    switch (state)
    {
    case StackFrameState::Raw:
        return "Raw";
    case StackFrameState::Calibrated:
        return "Calibrated";
    case StackFrameState::LocalNormalized:
        return "LocalNormalized";
    }
    return "UnknownStackFrameState";
}

Result<StackSourceStages> checkStackSourceContract(const StackModel& model, const StackSourceContract& contract)
{
    return checkContract(model, contract);
}

Result<StackSourceRows> stackSourceRowsOf(const StackSourceBand& frame, std::uint32_t firstRow, std::uint32_t rows)
{
    if (frame.samples == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kRowsOf, "frame.samples", "frame.samples is null");
    }
    if (frame.layout == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kRowsOf, "frame.layout", "frame.layout is null");
    }
    if (rows == 0u)
    {
        return refuse(ErrorCode::InvalidArgument, kRowsOf, "rows", "rows is zero, and a window holds at least one");
    }
    const ImageLayout& layout = *frame.layout;
    const ImageExtent& extent = layout.extent();
    if (firstRow >= extent.height() || static_cast<std::uint64_t>(firstRow) + rows > extent.height())
    {
        return failure(ErrorCode::OutOfRange, kRowsOf, "firstRow and rows do not lie inside the frame",
                       {StatusContextField::ofText(ContextKey::Parameter, "firstRow"),
                        StatusContextField::ofCount(ContextKey::Requested, static_cast<std::uint64_t>(firstRow) + rows),
                        StatusContextField::ofCount(ContextKey::Limit, extent.height())});
    }
    // Cropping narrows a validated source; its caller may have raised the default limits.
    const EngineLimits limits{extent.width(), extent.height(), extent.channelCount(), layout.totalBytes(),
                              layout.totalBytes()};
    const std::optional<ImageExtent> windowExtent =
        ImageExtent::create(extent.width(), rows, extent.channelCount(), limits);
    if (!windowExtent.has_value())
    {
        return failure(ErrorCode::LimitExceeded, kRowsOf, "the window's extent is not one this build can describe");
    }
    const ImageLayoutRequest request{.extent = *windowExtent,
                                     .elementSizeBytes = layout.elementSizeBytes(),
                                     .samplesPerPixel = layout.samplesPerPixel(),
                                     .planeCount = layout.planeCount(),
                                     .rowAlignmentBytes = 1u};
    const std::optional<ImageLayout> windowLayout =
        ImageLayout::createWithStrides(request, layout.rowStrideBytes(), layout.planeStrideBytes(), limits);
    if (!windowLayout.has_value())
    {
        return failure(ErrorCode::LimitExceeded, kRowsOf, "the window's layout is not one this build can describe");
    }

    StackSourceRows window;
    window.samples =
        static_cast<const std::byte*>(frame.samples) + static_cast<std::uint64_t>(firstRow) * layout.rowStrideBytes();
    window.layout = *windowLayout;
    window.window = frame;
    window.window.firstRow = frame.firstRow + firstRow;
    if (frame.validity.descriptor.needsMask())
    {
        if (frame.validity.mask == nullptr)
        {
            return refuse(ErrorCode::InvalidArgument, kRowsOf, "frame.validity.mask",
                          "frame.validity.mask is required by the explicit-mask rule and none was given");
        }
        const ValidityMaskView<const std::uint8_t>& mask = *frame.validity.mask;
        if (mask.width() != extent.width() || mask.height() != extent.height() ||
            (mask.planeCount() != 1u && mask.planeCount() != extent.channelCount()))
        {
            return refuse(ErrorCode::IncompatibleArguments, kRowsOf, "frame.validity.mask",
                          "frame.validity.mask does not cover the frame with one shared bit-plane or one per channel");
        }
        const std::optional<ImageExtent> maskExtent =
            ImageExtent::create(extent.width(), rows, mask.planeCount(), limits);
        const std::uint64_t skipped = static_cast<std::uint64_t>(firstRow) * mask.rowStrideBytes();
        const std::optional<ValidityMaskView<const std::uint8_t>> windowMask =
            maskExtent.has_value()
                ? ValidityMaskView<const std::uint8_t>::create(
                      std::span<const std::uint8_t>(mask.data() + skipped,
                                                    static_cast<std::size_t>(mask.spannedByteCount() - skipped)),
                      *maskExtent, mask.rowStrideBytes(), mask.planeStrideBytes())
                : std::nullopt;
        if (!windowMask.has_value())
        {
            return failure(ErrorCode::LimitExceeded, kRowsOf, "the window's mask is not one this build can describe");
        }
        window.mask = *windowMask;
    }
    else if (frame.validity.mask != nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kRowsOf, "frame.validity.mask",
                      "frame.validity.mask was supplied under a validity rule that reads no packed mask");
    }
    return window;
}

Result<ImageLayout> stackTileBandLayout(const StackModel& model, std::uint32_t rows)
{
    const ImageDescriptor& descriptor = model.request().input.descriptor;
    const ImageExtent& extent = descriptor.extent();
    if (rows == 0u || rows > extent.height())
    {
        return failure(ErrorCode::OutOfRange, kTileLayout, "rows is not between one and the frame's height",
                       {StatusContextField::ofText(ContextKey::Parameter, "rows"),
                        StatusContextField::ofCount(ContextKey::Requested, rows),
                        StatusContextField::ofCount(ContextKey::Limit, extent.height())});
    }
    // The descriptor already proves the dimensions. This helper describes conversion storage;
    // the planner and memory resource enforce the caller's allocation limits separately.
    const EngineLimits limits{extent.width(), extent.height(), extent.channelCount(),
                              (std::numeric_limits<std::uint64_t>::max)(), kDefaultMemoryBudgetBytes};
    const std::optional<ImageExtent> bandExtent =
        ImageExtent::create(extent.width(), rows, extent.channelCount(), limits);
    if (!bandExtent.has_value())
    {
        return failure(ErrorCode::LimitExceeded, kTileLayout, "the band's extent is not one this build can describe");
    }
    const ImageLayoutRequest request{.extent = *bandExtent,
                                     .elementSizeBytes =
                                         sampleTypeSizeBytes(stackTileSampleTypeOf(descriptor.sampleType())),
                                     .samplesPerPixel = 1u,
                                     .planeCount = extent.channelCount(),
                                     .rowAlignmentBytes = 1u};
    const std::optional<ImageLayout> layout = ImageLayout::create(request, limits);
    if (!layout.has_value())
    {
        return failure(ErrorCode::LimitExceeded, kTileLayout, "the band's layout is not one this build can describe");
    }
    return *layout;
}

Result<StackSourceOutcome> convertStackSourceBand(const ExecutionContext& execution, const StackModel& model,
                                                  const StackSourceContract& contract, const StackSourceBand& source,
                                                  std::span<const double> frameNormalization,
                                                  std::span<const std::span<std::byte>> workerScratch,
                                                  const StackTileBand& tile)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kConvert, "the conversion was given an empty execution context");
    }
    const Status checkpoint = execution.checkCancellation();
    if (checkpoint.failed())
    {
        return checkpoint;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (!isStackFrameState(contract.state))
    {
        return refuse(ErrorCode::InvalidArgument, kConvert, "contract.state",
                      "contract.state is not a state this build defines");
    }
    const Result<BandGeometry> geometry = checkBands(model, source, tile);
    if (!geometry.hasValue())
    {
        return geometry.status();
    }
    const StackRequest& request = model.request();
    const StackSourceStages stages = stackSourceStagesOf(model, contract);

    BandRun run;
    run.model = &model;
    run.contract = &contract;
    run.source = &source;
    run.tile = &tile;
    run.geometry = *geometry;
    run.stages = stages;
    run.policy = conversionPolicyFor(request.input.descriptor.numericDomain());

    // The band's rule as the stages downstream of calibration see it, and as the tile carries it.
    const ValidityDescriptor& sourceRule = source.validity.descriptor;
    if (stages.calibrate)
    {
        if (contract.state != StackFrameState::Raw)
        {
            return refuse(ErrorCode::IncompatibleArguments, kConvert, "contract.calibration",
                          "contract.calibration was supplied for frames that are not raw");
        }
        if (contract.calibration.size() != 1u && contract.calibration.size() != request.input.frameCount)
        {
            return refuse(ErrorCode::IncompatibleArguments, kConvert, "contract.calibration",
                          "contract.calibration holds neither one plan for every frame nor one per frame");
        }
        const Status checked = checkPlan(model, contract, source.frameIndex, kConvert);
        if (checked.failed())
        {
            return checked;
        }
        run.plan = &planOf(contract, source.frameIndex);
        const CfaDescriptor& cfa = request.input.descriptor.cfa();
        run.cfaSide = cfa.isMosaiced() ? cfaPatternBlockSize(cfa.pattern()) : 0u;
        run.outputValidity = *ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::FiniteOnly});
    }
    else if (contract.masters != nullptr)
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "contract.masters",
                      "contract.masters were supplied without a plan that applies them");
    }
    else if (sourceRule.rule() == ValidityRule::DomainRange)
    {
        // Range membership is decided on stored samples. Rounded endpoints cannot preserve it:
        // distinct UInt32 codes can even become the same Float32 value. The row writes excluded
        // samples as NaNs, carrying that original membership without allocating a packed mask.
        run.outputValidity = *ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::FiniteOnly});
    }
    else
    {
        run.outputValidity = sourceRule;
    }
    if (!stages.calibrate && sourceRule.rule() == ValidityRule::DomainRange &&
        (stages.normalizeLocally || stages.normalizeGlobally))
    {
        return failure(ErrorCode::Unsupported, kConvert,
                       "a stated value range describes the untransformed samples and cannot be carried through a "
                       "normalization; restate it after normalizing",
                       {StatusContextField::ofText(ContextKey::Parameter, "source.validity")});
    }

    if (stages.normalizeLocally)
    {
        const LocalNormalizationDocument* document = contract.localNormalization;
        if (document == nullptr)
        {
            return refuse(ErrorCode::InvalidArgument, kConvert, "contract.localNormalization",
                          "contract.localNormalization is null under a local stage, whose surface it carries");
        }
        const Status shape = checkDocumentShape(model, *document, kConvert);
        if (shape.failed())
        {
            return shape;
        }
        std::uint32_t firstNodeRow = 0u;
        std::uint32_t lastNodeRow = 0u;
        nodeRowsOf(*document, source.firstRow, source.firstRow + geometry->rows - 1u, firstNodeRow, lastNodeRow);
        const Status record =
            checkDocumentFrame(model, *document, source.frameIndex, firstNodeRow, lastNodeRow, kConvert);
        if (record.failed())
        {
            return record;
        }
        if (document->validity != run.outputValidity.rule())
        {
            return refuse(ErrorCode::IncompatibleArguments, kConvert, "source.validity",
                          "source.validity is not the rule the local-normalization document was fitted under");
        }
        run.record = &document->frames[source.frameIndex];
        run.applyLocal = !document->referenceIndex.has_value() || *document->referenceIndex != source.frameIndex;
        if (run.applyLocal)
        {
            run.sourceScratchBytes = stackSourceScratchBytesPerWorker(model);
            const std::uint32_t workers = execution.admittedConcurrency() > 0u ? execution.admittedConcurrency() : 1u;
            if (workerScratch.size() < workers)
            {
                return failure(ErrorCode::IncompatibleArguments, kConvert,
                               "workerScratch holds fewer slices than the execution was admitted workers",
                               {StatusContextField::ofText(ContextKey::Parameter, "workerScratch"),
                                StatusContextField::ofCount(ContextKey::Requested, workerScratch.size()),
                                StatusContextField::ofCount(ContextKey::Limit, workers)});
            }
            const std::uint64_t needed = stackScratchBytesPerWorker(model);
            for (const std::span<std::byte>& slice : workerScratch)
            {
                if (slice.size() < needed)
                {
                    return failure(ErrorCode::IncompatibleArguments, kConvert,
                                   "workerScratch holds a slice shorter than the model's per-worker contract",
                                   {StatusContextField::ofText(ContextKey::Parameter, "workerScratch"),
                                    StatusContextField::ofCount(ContextKey::Requested, slice.size()),
                                    StatusContextField::ofCount(ContextKey::Limit, needed)});
                }
                if (!pointerIsAligned(slice.data(), kStackScratchAlignmentBytes))
                {
                    return refuse(ErrorCode::InvalidArgument, kConvert, "workerScratch",
                                  "workerScratch holds a slice that does not begin on the scratch alignment");
                }
            }
            run.workerScratch = workerScratch;
        }
    }
    else if (contract.localNormalization != nullptr)
    {
        return refuse(
            ErrorCode::IncompatibleArguments, kConvert, "contract.localNormalization",
            contract.state == StackFrameState::LocalNormalized
                ? "contract.localNormalization was supplied for frames already brought to the reference"
                : "contract.localNormalization was supplied for a stack whose scope applies no local surface");
    }

    if (stages.normalizeGlobally)
    {
        const Result<std::uint64_t> entries = tableEntriesOf(model, kConvert);
        if (!entries.hasValue())
        {
            return entries.status();
        }
        if (frameNormalization.size() != *entries)
        {
            return failure(ErrorCode::IncompatibleArguments, kConvert,
                           "frameNormalization does not hold an offset and a scale per frame and channel",
                           {StatusContextField::ofText(ContextKey::Parameter, "frameNormalization"),
                            StatusContextField::ofCount(ContextKey::Requested, frameNormalization.size()),
                            StatusContextField::ofCount(ContextKey::Limit, *entries)});
        }
        const std::uint64_t first =
            static_cast<std::uint64_t>(source.frameIndex) * geometry->channels * kCoefficientEntries;
        for (std::uint64_t entry = first;
             entry < first + static_cast<std::uint64_t>(geometry->channels) * kCoefficientEntries; ++entry)
        {
            if (!finiteValue(frameNormalization[entry]) ||
                (entry % kCoefficientEntries == 1u && frameNormalization[entry] == 0.0))
            {
                return failure(ErrorCode::InvalidArgument, kConvert,
                               "frameNormalization holds a non-finite coefficient or a zero scale for this frame",
                               {StatusContextField::ofText(ContextKey::Parameter, "frameNormalization"),
                                StatusContextField::ofCount(ContextKey::Requested, source.frameIndex)});
            }
        }
        run.table = frameNormalization;
        run.multiplicative = normalizationMethodIsMultiplicative(*request.normalization.globalMethod);
    }
    else if (!frameNormalization.empty())
    {
        return refuse(ErrorCode::IncompatibleArguments, kConvert, "frameNormalization",
                      "frameNormalization was supplied for a stack whose scope applies no global match");
    }

    StackSourceOutcome outcome;
    outcome.stages = stages;
    outcome.backend = *selected;
    outcome.inPlace = geometry->inPlace;
    outcome.validity = run.outputValidity;
    const bool reference = selected->backend == ComputeBackend::Reference;
    BandCounters counters;
    bool compiledUsed = false;
    const Status ran =
        runBand(execution, run, reference ? nullptr : stackSourceRunsFor(execution), counters, compiledUsed);
    if (ran.failed())
    {
        return ran;
    }
    if (!counters.finite.load(std::memory_order_relaxed))
    {
        return failure(ErrorCode::InvalidData, kConvert,
                       "a sample the frame's validity rule admits is not a finite number; state the rule that "
                       "excludes it",
                       {StatusContextField::ofText(ContextKey::Parameter, "source.samples"),
                        StatusContextField::ofCount(ContextKey::Requested, source.frameIndex)});
    }
    outcome.samplesWritten = counters.written.load(std::memory_order_relaxed);
    outcome.samplesTransformed = counters.transformed.load(std::memory_order_relaxed);
    outcome.samplesPreserved = counters.preserved.load(std::memory_order_relaxed);
    outcome.invalidCalibrationInputs = counters.invalidInputs.load(std::memory_order_relaxed);
    outcome.nonFiniteOutputs = counters.nonFinite.load(std::memory_order_relaxed);
    outcome.noDataCollisions = counters.collisions.load(std::memory_order_relaxed);
    outcome.compiledRowUsed = compiledUsed;
    return outcome;
}

Status storeStackFrameNormalization(const StackModel& model, std::uint32_t frameIndex, const FrameNormalization& match,
                                    std::span<double> table)
{
    const StackRequest& request = model.request();
    const Result<std::uint64_t> entries = tableEntriesOf(model, kStore);
    if (!entries.hasValue())
    {
        return entries.status();
    }
    if (frameIndex >= request.input.frameCount)
    {
        return failure(ErrorCode::OutOfRange, kStore, "frameIndex is not below the stack's frame count",
                       {StatusContextField::ofText(ContextKey::Parameter, "frameIndex"),
                        StatusContextField::ofCount(ContextKey::Requested, frameIndex),
                        StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount)});
    }
    if (table.size() != *entries)
    {
        return failure(ErrorCode::IncompatibleArguments, kStore,
                       "table does not hold an offset and a scale per frame and channel",
                       {StatusContextField::ofText(ContextKey::Parameter, "table"),
                        StatusContextField::ofCount(ContextKey::Requested, table.size()),
                        StatusContextField::ofCount(ContextKey::Limit, *entries)});
    }
    const Status checked = checkMatch(model, match, frameIndex, "match", kStore);
    if (checked.failed())
    {
        return checked;
    }
    const std::uint32_t channels = request.input.descriptor.extent().channelCount();
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::uint64_t entry = (static_cast<std::uint64_t>(frameIndex) * channels + channel) * kCoefficientEntries;
        table[entry] = match.channels[channel].offset;
        table[entry + 1u] = match.channels[channel].scale;
    }
    return Status::success();
}

Result<FrameNormalization> stackFrameNormalizationAt(const StackModel& model, std::uint32_t frameIndex,
                                                     std::span<const double> table)
{
    const StackRequest& request = model.request();
    const Result<std::uint64_t> entries = tableEntriesOf(model, kAt);
    if (!entries.hasValue())
    {
        return entries.status();
    }
    if (frameIndex >= request.input.frameCount)
    {
        return failure(ErrorCode::OutOfRange, kAt, "frameIndex is not below the stack's frame count",
                       {StatusContextField::ofText(ContextKey::Parameter, "frameIndex"),
                        StatusContextField::ofCount(ContextKey::Requested, frameIndex),
                        StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount)});
    }
    if (table.size() != *entries)
    {
        return failure(ErrorCode::IncompatibleArguments, kAt,
                       "table does not hold an offset and a scale per frame and channel",
                       {StatusContextField::ofText(ContextKey::Parameter, "table"),
                        StatusContextField::ofCount(ContextKey::Requested, table.size()),
                        StatusContextField::ofCount(ContextKey::Limit, *entries)});
    }
    const std::uint32_t channels = request.input.descriptor.extent().channelCount();
    FrameNormalization match;
    match.method = *request.normalization.globalMethod;
    match.estimator = request.normalization.scaleEstimator;
    match.channelCount = channels;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::uint64_t entry = (static_cast<std::uint64_t>(frameIndex) * channels + channel) * kCoefficientEntries;
        match.channels[channel] = NormalizationChannelCoefficients{.scale = table[entry + 1u], .offset = table[entry]};
    }
    return match;
}

Result<LocalBackgroundFitResult> beginStackLocalNormalization(const ExecutionContext& execution,
                                                              const StackModel& model,
                                                              const LocalNormalizationRequest& request,
                                                              const NormalizationFrameView& reference,
                                                              LocalNormalizationDocument& document)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kBegin, "the fit was given an empty execution context");
    }
    const StackRequest& stack = model.request();
    if (!scopeIsLocal(stack.normalization.scope))
    {
        return refuse(ErrorCode::IncompatibleArguments, kBegin, "model",
                      "model applies no local surface, so there is no document to start");
    }
    const Status frame = checkFitFrame(model, reference, "reference", kBegin);
    if (frame.failed())
    {
        return frame;
    }
    const ImageExtent& extent = stack.input.descriptor.extent();
    const Result<LocalNormalizationPlan> plan =
        planLocalNormalization(request, extent.width(), extent.height(), extent.channelCount());
    if (!plan.hasValue())
    {
        return plan.status();
    }

    LocalNormalizationDocument started;
    started.request = request;
    started.shape = plan->shape();
    started.validity = reference.validity().descriptor.rule();
    started.referenceIndex = stack.normalization.referenceFrame;
    started.referenceBackground.assign(static_cast<std::size_t>(plan->shape().valueCount()), 0.0);
    const Result<LocalBackgroundFitResult> fitted =
        fitLocalBackgroundGrid(execution, reference, *plan, started.referenceBackground);
    if (!fitted.hasValue())
    {
        return fitted.status();
    }
    started.frames.resize(stack.input.frameCount);
    for (std::uint32_t index = 0u; index < stack.input.frameCount; ++index)
    {
        started.frames[index].frameIndex = index;
    }
    started.frames[stack.normalization.referenceFrame].background = started.referenceBackground;
    document = std::move(started);
    return *fitted;
}

Result<StackLocalNormalizationFit>
fitStackLocalNormalizationFrame(const ExecutionContext& execution, const StackModel& model,
                                const NormalizationFrameView& frame, std::uint32_t frameIndex,
                                const NormalizationFrameView& reference, bool fitScaleGrid,
                                LocalNormalizationDocument& document)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kFit, "the fit was given an empty execution context");
    }
    const StackRequest& stack = model.request();
    if (!scopeIsLocal(stack.normalization.scope))
    {
        return refuse(ErrorCode::IncompatibleArguments, kFit, "model",
                      "model applies no local surface, so there is no document to fit into");
    }
    const Status frameChecked = checkFitFrame(model, frame, "frame", kFit);
    if (frameChecked.failed())
    {
        return frameChecked;
    }
    const Status referenceChecked = checkFitFrame(model, reference, "reference", kFit);
    if (referenceChecked.failed())
    {
        return referenceChecked;
    }
    if (frameIndex >= stack.input.frameCount)
    {
        return failure(ErrorCode::OutOfRange, kFit, "frameIndex is not below the stack's frame count",
                       {StatusContextField::ofText(ContextKey::Parameter, "frameIndex"),
                        StatusContextField::ofCount(ContextKey::Requested, frameIndex),
                        StatusContextField::ofCount(ContextKey::Limit, stack.input.frameCount)});
    }
    const ImageExtent& extent = stack.input.descriptor.extent();
    if (!document.referenceIndex.has_value() || *document.referenceIndex != stack.normalization.referenceFrame ||
        document.frames.size() != stack.input.frameCount || document.shape.width != extent.width() ||
        document.shape.height != extent.height() || document.shape.channelCount != extent.channelCount() ||
        document.referenceBackground.size() != document.shape.valueCount())
    {
        return failure(ErrorCode::InvalidState, kFit,
                       "the document was not started for this stack; begin it from the reference frame first",
                       {StatusContextField::ofText(ContextKey::Parameter, "document")});
    }
    StackLocalNormalizationFit fit;
    if (frameIndex == *document.referenceIndex)
    {
        fit.isReference = true;
        return fit;
    }
    const Result<LocalNormalizationPlan> plan =
        planLocalNormalization(document.request, extent.width(), extent.height(), extent.channelCount());
    if (!plan.hasValue())
    {
        return plan.status();
    }

    std::vector<double> background(static_cast<std::size_t>(plan->shape().valueCount()), 0.0);
    const Result<LocalBackgroundFitResult> fitted = fitLocalBackgroundGrid(execution, frame, *plan, background);
    if (!fitted.hasValue())
    {
        return fitted.status();
    }
    fit.background = *fitted;
    const Result<LocalScaleEstimate> scale =
        estimateLocalScale(execution, frame, background, reference, document.referenceBackground, *plan);
    if (!scale.hasValue())
    {
        return scale.status();
    }
    fit.scale = *scale;
    LocalNormalizationFrameCoefficients record;
    record.frameIndex = frameIndex;
    for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
    {
        record.scale[channel] = scale->scale[channel];
    }
    if (fitScaleGrid)
    {
        std::vector<double> surface(static_cast<std::size_t>(plan->shape().valueCount()), 0.0);
        const Result<LocalScaleGridResult> grid =
            fitLocalScaleGrid(execution, frame, background, reference, document.referenceBackground, *plan,
                              std::span<const double>(record.scale.data(), extent.channelCount()), surface);
        if (!grid.hasValue())
        {
            return grid.status();
        }
        fit.scaleGrid = *grid;
        record.scaleGrid = std::move(surface);
    }
    record.background = std::move(background);
    document.frames[frameIndex] = std::move(record);
    return fit;
}

} // namespace an
