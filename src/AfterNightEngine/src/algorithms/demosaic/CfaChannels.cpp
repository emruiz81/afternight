// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Demosaic/CfaChannels.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/demosaic/DemosaicInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

// Splitting a mosaic, joining it back, and averaging each block into one colour pixel.
//
// None of these estimates anything, so none of them has a numeric domain, a clip point or a
// directional decision. What they do have is the mosaic's phase, and that is the whole reason they
// live beside the demosaic rather than beside the layout transfers: which filter a plane holds is a
// question about where the repeating block starts, and a caller that cropped an odd number of
// columns has a different answer from one that did not.
//
// The split keeps positions rather than colours. Plane `row * blockSize + column` is the sample at
// that position of every block, whatever filter covers it - so the round trip through the join is an
// identity, and a caller that wants "the red plane" asks the mosaic which position red sits at.
// Reordering the planes by colour here would hide the phase behind an assumption: a named Bayer
// block holds two greens, so a colour order would still have to choose which green comes first.
// A tabulated mosaic is not split at all; its block has thirty-six positions and nothing asks for
// them.

namespace an
{
namespace
{

using demosaic_detail::FloatPlane;

constexpr const char* kSplitOperation = "an::splitCfaBlockPlanes";
constexpr const char* kJoinOperation = "an::joinCfaBlockPlanes";
constexpr const char* kSuperPixelPlanOperation = "an::planCfaSuperPixel";
constexpr const char* kSuperPixelOperation = "an::superPixelCfa";

/// The block geometry two images have to agree about before a rearrangement runs.
struct BlockGeometry
{
    /// How many pixels of the mosaic one block covers in each direction.
    std::uint32_t blockSize = 0u;

    /// How many whole blocks fit across the mosaic.
    std::uint32_t blockColumns = 0u;

    /// How many whole blocks fit down the mosaic.
    std::uint32_t blockRows = 0u;

    /// Mosaic columns left out because they do not complete a block.
    std::uint32_t trailingColumns = 0u;

    /// Mosaic rows left out because they do not complete a block.
    std::uint32_t trailingRows = 0u;
};

/// How the blocks of one mosaic divide it.
///
/// @param blockSize how many pixels a block covers in each direction.
/// @param width how many pixels the mosaic holds across.
/// @param height how many rows the mosaic holds.
/// @return the block counts and what is left over.
[[nodiscard]] BlockGeometry blockGeometryOf(std::uint32_t blockSize, std::uint32_t width, std::uint32_t height) noexcept
{
    BlockGeometry geometry{};
    geometry.blockSize = blockSize;
    geometry.blockColumns = width / blockSize;
    geometry.blockRows = height / blockSize;
    geometry.trailingColumns = width % blockSize;
    geometry.trailingRows = height % blockSize;
    return geometry;
}

/// What one row of a rearrangement costs.
///
/// @param width how many block columns the row covers.
/// @param samples how many samples each block contributes.
/// @return the bytes one row touches and roughly what it costs.
[[nodiscard]] TaskCost blockRowCost(std::uint32_t width, std::uint32_t samples) noexcept
{
    const std::uint64_t total = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(samples);
    return TaskCost{.bytesPerItem = total * 8u, .operationsPerItem = total * 2u};
}

/// Adds one worker's conversion counts to the operation totals.
///
/// @param local the worker-local counts.
/// @param converted destination samples written.
/// @param nonFinite non-finite computed samples met.
/// @param saturated computed samples outside the destination range.
void mergeConversionCounters(const ConversionCounters& local, std::atomic<std::uint64_t>& converted,
                             std::atomic<std::uint64_t>& nonFinite, std::atomic<std::uint64_t>& saturated) noexcept
{
    converted.fetch_add(local.samplesConverted, std::memory_order_relaxed);
    nonFinite.fetch_add(local.nonFiniteInputs, std::memory_order_relaxed);
    saturated.fetch_add(local.saturatedSamples, std::memory_order_relaxed);
}

/// A super-pixel write-back refusal.
///
/// @param code whether a non-finite or range decision refused.
/// @param completed the destination-sample position reached.
/// @param total how many destination samples the operation intended to write.
/// @return the failure carrying both counts.
[[nodiscard]] Status superPixelConversionFailure(ErrorCode code, std::uint64_t completed, std::uint64_t total)
{
    return Status::failure(
        code, StatusDetail{.message = code == ErrorCode::InvalidData
                                          ? "a super-pixel output is not finite and write back refuses one"
                                          : "a super-pixel output is outside the destination representation and write "
                                            "back refuses one",
                           .operation = kSuperPixelOperation,
                           .domain = ErrorDomain::Algorithms,
                           .context = {StatusContextField::ofCount(ContextKey::Completed, completed),
                                       StatusContextField::ofCount(ContextKey::Total, total)}});
}

} // namespace

Result<CfaChannelResult> splitCfaBlockPlanes(const ExecutionContext& execution, SampleType sampleType,
                                             const void* source, const ImageLayout& sourceLayout,
                                             const CfaDescriptor& cfa, void* destination,
                                             const ImageLayout& destinationLayout, const CfaChannelSelection& selection)
{
    const std::uint32_t planeCount = cfaBlockPlaneCount(cfa.pattern());
    if (planeCount == 0u)
    {
        return demosaic_detail::demosaicFailure(
            ErrorCode::IncompatibleArguments, kSplitOperation,
            "only a mosaic whose repeating block is named splits into one plane per position");
    }
    const Status checked = demosaic_detail::checkImagePair(execution, sampleType, source, sourceLayout, 1u, destination,
                                                           destinationLayout, planeCount, kSplitOperation);
    if (checked.failed())
    {
        return checked;
    }
    const std::uint32_t blockSize = cfaPatternBlockSize(cfa.pattern());
    const BlockGeometry geometry =
        blockGeometryOf(blockSize, sourceLayout.extent().width(), sourceLayout.extent().height());
    if (destinationLayout.extent().width() != geometry.blockColumns ||
        destinationLayout.extent().height() != geometry.blockRows)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kSplitOperation,
                                                "the destination is not the mosaic divided by its block size");
    }
    const Status sourceMask =
        demosaic_detail::checkSelectionMask(selection.validity, selection.mask, sourceLayout.extent().width(),
                                            sourceLayout.extent().height(), 1u, 0u, kSplitOperation);
    if (sourceMask.failed())
    {
        return sourceMask;
    }
    if (selection.validity.needsMask() != (selection.produced != nullptr))
    {
        return demosaic_detail::demosaicFailure(
            ErrorCode::IncompatibleArguments, kSplitOperation,
            selection.validity.needsMask()
                ? "an explicit source mask must be split into one produced plane per block position"
                : "a value validity rule is preserved without materialising a produced mask");
    }
    const Status producedMask = demosaic_detail::checkProducedMask(selection.produced, geometry.blockColumns,
                                                                   geometry.blockRows, planeCount, 0u, kSplitOperation);
    if (producedMask.failed())
    {
        return producedMask;
    }
    const Status maskAliasing = demosaic_detail::checkMaskAliasing(source, sourceLayout.totalBytes(), destination,
                                                                   destinationLayout.totalBytes(), selection.mask,
                                                                   selection.produced, kSplitOperation);
    if (maskAliasing.failed())
    {
        return maskAliasing;
    }
    if (selection.produced != nullptr)
    {
        selection.produced->fill(false);
    }

    const std::uint32_t destinationColumnStride = destinationLayout.samplesPerPixel();
    const bool allValid = selection.validity.rule() == ValidityRule::AllValid;
    const KernelValidityView validity{.descriptor = selection.validity, .mask = selection.mask, .maskPlane = 0u};
    std::atomic<std::uint64_t> meaningful{0u};
    Status ran = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        if (blockSize == 2u && allValid && selection.produced == nullptr)
        {
            ran = parallelFor(
                execution, geometry.blockRows, blockRowCost(geometry.blockColumns, 4u), [&](TaskRange range) {
                    const std::size_t sourceStep = sourceLayout.samplesPerPixel();
                    for (std::uint64_t blockRow = range.begin; blockRow < range.end; ++blockRow)
                    {
                        const auto* const firstSourceRow = reinterpret_cast<const SampleT*>(
                            static_cast<const std::uint8_t*>(source) +
                            demosaic_detail::channelRowByteOffset(sourceLayout, 0u, blockRow * 2u));
                        const auto* const secondSourceRow = reinterpret_cast<const SampleT*>(
                            static_cast<const std::uint8_t*>(source) +
                            demosaic_detail::channelRowByteOffset(sourceLayout, 0u, blockRow * 2u + 1u));
                        std::array<SampleT*, 4u> planes{};
                        for (std::uint32_t channel = 0u; channel < 4u; ++channel)
                        {
                            planes[channel] = reinterpret_cast<SampleT*>(
                                static_cast<std::uint8_t*>(destination) +
                                demosaic_detail::channelRowByteOffset(destinationLayout, channel, blockRow));
                        }
                        for (std::uint32_t blockColumn = 0u; blockColumn < geometry.blockColumns; ++blockColumn)
                        {
                            const std::size_t sourceColumn = static_cast<std::size_t>(blockColumn) * 2u * sourceStep;
                            const std::size_t destinationColumn =
                                static_cast<std::size_t>(blockColumn) * destinationColumnStride;
                            planes[0u][destinationColumn] = firstSourceRow[sourceColumn];
                            planes[1u][destinationColumn] = firstSourceRow[sourceColumn + sourceStep];
                            planes[2u][destinationColumn] = secondSourceRow[sourceColumn];
                            planes[3u][destinationColumn] = secondSourceRow[sourceColumn + sourceStep];
                        }
                    }
                    return Status::success();
                });
            return;
        }
        ran = parallelFor(
            execution, geometry.blockRows, blockRowCost(geometry.blockColumns, blockSize * blockSize),
            [&](TaskRange range) {
                std::uint64_t chunkValid = 0u;
                for (std::uint64_t blockRow = range.begin; blockRow < range.end; ++blockRow)
                {
                    for (std::uint32_t withinRow = 0u; withinRow < blockSize; ++withinRow)
                    {
                        const std::uint64_t sourceRow = blockRow * blockSize + withinRow;
                        const auto* const sourceSamples = reinterpret_cast<const SampleT*>(
                            static_cast<const std::uint8_t*>(source) +
                            demosaic_detail::channelRowByteOffset(sourceLayout, 0u, sourceRow));
                        for (std::uint32_t withinColumn = 0u; withinColumn < blockSize; ++withinColumn)
                        {
                            const std::uint32_t channel = withinRow * blockSize + withinColumn;
                            auto* const destinationSamples = reinterpret_cast<SampleT*>(
                                static_cast<std::uint8_t*>(destination) +
                                demosaic_detail::channelRowByteOffset(destinationLayout, channel, blockRow));
                            for (std::uint32_t blockColumn = 0u; blockColumn < geometry.blockColumns; ++blockColumn)
                            {
                                const std::uint32_t sourceColumn = blockColumn * blockSize + withinColumn;
                                const SampleT value = sourceSamples[sourceColumn * sourceLayout.samplesPerPixel()];
                                destinationSamples[static_cast<std::size_t>(blockColumn) * destinationColumnStride] =
                                    value;
                                const bool sampleValid =
                                    allValid || (sampleIsValidByValue(selection.validity, value) &&
                                                 plane_detail::maskAllows(validity, sourceColumn,
                                                                          static_cast<std::uint32_t>(sourceRow)));
                                chunkValid += sampleValid ? 1u : 0u;
                                if (selection.produced != nullptr)
                                {
                                    static_cast<void>(selection.produced->setValid(
                                        channel, blockColumn, static_cast<std::uint32_t>(blockRow), sampleValid));
                                }
                            }
                        }
                    }
                }
                meaningful.fetch_add(chunkValid, std::memory_order_relaxed);
                return Status::success();
            });
    });
    if (!known)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kSplitOperation,
                                                "the representation is not one of the eight this engine stores");
    }
    if (ran.failed())
    {
        return ran;
    }
    if (selection.produced != nullptr)
    {
        selection.produced->clearPadBits();
    }

    CfaChannelResult result{};
    result.pattern = cfa.pattern();
    result.blockSize = blockSize;
    result.blockColumns = geometry.blockColumns;
    result.blockRows = geometry.blockRows;
    result.trailingColumns = geometry.trailingColumns;
    result.trailingRows = geometry.trailingRows;
    result.plannedWorkers =
        plannedWorkerCount(execution, geometry.blockRows, blockRowCost(geometry.blockColumns, blockSize * blockSize));
    const std::uint64_t covered = static_cast<std::uint64_t>(geometry.blockColumns) *
                                  static_cast<std::uint64_t>(geometry.blockRows) * blockSize * blockSize;
    result.run.selection.total = covered;
    result.run.selection.valid = allValid ? covered : meaningful.load(std::memory_order_relaxed);
    result.run.outputsWritten = covered;
    result.outputValidity = selection.validity;
    return result;
}

Result<CfaChannelResult> joinCfaBlockPlanes(const ExecutionContext& execution, SampleType sampleType,
                                            const void* source, const ImageLayout& sourceLayout,
                                            const CfaDescriptor& cfa, void* destination,
                                            const ImageLayout& destinationLayout, const CfaChannelSelection& selection)
{
    const std::uint32_t planeCount = cfaBlockPlaneCount(cfa.pattern());
    if (planeCount == 0u)
    {
        return demosaic_detail::demosaicFailure(
            ErrorCode::IncompatibleArguments, kJoinOperation,
            "only a mosaic whose repeating block is named is rebuilt from one plane per position");
    }
    const Status checked = demosaic_detail::checkImagePair(execution, sampleType, source, sourceLayout, planeCount,
                                                           destination, destinationLayout, 1u, kJoinOperation);
    if (checked.failed())
    {
        return checked;
    }
    const std::uint32_t blockSize = cfaPatternBlockSize(cfa.pattern());
    const BlockGeometry geometry =
        blockGeometryOf(blockSize, destinationLayout.extent().width(), destinationLayout.extent().height());
    if (sourceLayout.extent().width() != geometry.blockColumns || sourceLayout.extent().height() != geometry.blockRows)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kJoinOperation,
                                                "the plane image is not the mosaic divided by its block size");
    }
    const Status sourceMask =
        demosaic_detail::checkSelectionMask(selection.validity, selection.mask, sourceLayout.extent().width(),
                                            sourceLayout.extent().height(), 1u, planeCount, kJoinOperation);
    if (sourceMask.failed())
    {
        return sourceMask;
    }
    if (selection.validity.needsMask() != (selection.produced != nullptr))
    {
        return demosaic_detail::demosaicFailure(
            ErrorCode::IncompatibleArguments, kJoinOperation,
            selection.validity.needsMask()
                ? "explicit source masks must be joined into one produced mosaic mask"
                : "a value validity rule is preserved without materialising a produced mask");
    }
    const Status producedMask =
        demosaic_detail::checkProducedMask(selection.produced, destinationLayout.extent().width(),
                                           destinationLayout.extent().height(), 1u, 0u, kJoinOperation);
    if (producedMask.failed())
    {
        return producedMask;
    }
    const Status maskAliasing = demosaic_detail::checkMaskAliasing(source, sourceLayout.totalBytes(), destination,
                                                                   destinationLayout.totalBytes(), selection.mask,
                                                                   selection.produced, kJoinOperation);
    if (maskAliasing.failed())
    {
        return maskAliasing;
    }
    if (selection.produced != nullptr)
    {
        selection.produced->fill(false);
    }

    const std::uint32_t sourceColumnStride = sourceLayout.samplesPerPixel();
    const bool allValid = selection.validity.rule() == ValidityRule::AllValid;
    std::atomic<std::uint64_t> meaningful{0u};
    Status ran = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        ran = parallelFor(
            execution, geometry.blockRows, blockRowCost(geometry.blockColumns, blockSize * blockSize),
            [&](TaskRange range) {
                std::uint64_t chunkValid = 0u;
                for (std::uint64_t blockRow = range.begin; blockRow < range.end; ++blockRow)
                {
                    for (std::uint32_t withinRow = 0u; withinRow < blockSize; ++withinRow)
                    {
                        const std::uint64_t destinationRow = blockRow * blockSize + withinRow;
                        auto* const destinationSamples = reinterpret_cast<SampleT*>(
                            static_cast<std::uint8_t*>(destination) +
                            demosaic_detail::channelRowByteOffset(destinationLayout, 0u, destinationRow));
                        for (std::uint32_t withinColumn = 0u; withinColumn < blockSize; ++withinColumn)
                        {
                            const std::uint32_t channel = withinRow * blockSize + withinColumn;
                            const auto* const sourceSamples = reinterpret_cast<const SampleT*>(
                                static_cast<const std::uint8_t*>(source) +
                                demosaic_detail::channelRowByteOffset(sourceLayout, channel, blockRow));
                            const KernelValidityView validity{
                                .descriptor = selection.validity,
                                .mask = selection.mask,
                                .maskPlane =
                                    selection.mask != nullptr && selection.mask->planeCount() != 1u ? channel : 0u};
                            for (std::uint32_t blockColumn = 0u; blockColumn < geometry.blockColumns; ++blockColumn)
                            {
                                const std::uint32_t destinationColumn = blockColumn * blockSize + withinColumn;
                                const SampleT value =
                                    sourceSamples[static_cast<std::size_t>(blockColumn) * sourceColumnStride];
                                destinationSamples[destinationColumn * destinationLayout.samplesPerPixel()] = value;
                                const bool sampleValid =
                                    allValid || (sampleIsValidByValue(selection.validity, value) &&
                                                 plane_detail::maskAllows(validity, blockColumn,
                                                                          static_cast<std::uint32_t>(blockRow)));
                                chunkValid += sampleValid ? 1u : 0u;
                                if (selection.produced != nullptr)
                                {
                                    static_cast<void>(selection.produced->setValid(
                                        0u, destinationColumn, static_cast<std::uint32_t>(destinationRow),
                                        sampleValid));
                                }
                            }
                        }
                    }
                }
                meaningful.fetch_add(chunkValid, std::memory_order_relaxed);
                return Status::success();
            });
    });
    if (!known)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kJoinOperation,
                                                "the representation is not one of the eight this engine stores");
    }
    if (ran.failed())
    {
        return ran;
    }
    if (selection.produced != nullptr)
    {
        selection.produced->clearPadBits();
    }

    CfaChannelResult result{};
    result.pattern = cfa.pattern();
    result.blockSize = blockSize;
    result.blockColumns = geometry.blockColumns;
    result.blockRows = geometry.blockRows;
    result.trailingColumns = geometry.trailingColumns;
    result.trailingRows = geometry.trailingRows;
    result.plannedWorkers =
        plannedWorkerCount(execution, geometry.blockRows, blockRowCost(geometry.blockColumns, blockSize * blockSize));
    const std::uint64_t covered = static_cast<std::uint64_t>(geometry.blockColumns) *
                                  static_cast<std::uint64_t>(geometry.blockRows) * blockSize * blockSize;
    result.run.selection.total = covered;
    result.run.selection.valid = allValid ? covered : meaningful.load(std::memory_order_relaxed);
    result.run.outputsWritten = covered;
    result.outputValidity = selection.validity;
    return result;
}

Status planCfaSuperPixel(const CfaDescriptor& cfa)
{
    const std::uint32_t blockSize = cfaSuperPixelBlockSize(cfa.pattern());
    if (blockSize == 0u)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kSuperPixelPlanOperation,
                                                "an image with no filter array has no block to average");
    }
    const std::uint32_t patternBlock = cfaPatternBlockSize(cfa.pattern());
    for (std::uint32_t blockRow = 0u; blockRow * blockSize < patternBlock; ++blockRow)
    {
        for (std::uint32_t blockColumn = 0u; blockColumn * blockSize < patternBlock; ++blockColumn)
        {
            std::array<std::uint32_t, 3> count{};
            for (std::uint32_t withinRow = 0u; withinRow < blockSize; ++withinRow)
            {
                for (std::uint32_t withinColumn = 0u; withinColumn < blockSize; ++withinColumn)
                {
                    const std::uint32_t x = blockColumn * blockSize + withinColumn;
                    const std::uint32_t y = blockRow * blockSize + withinRow;
                    ++count[static_cast<std::size_t>(cfa.colorAt(x, y))];
                }
            }
            if (count[0] == 0u || count[1] == 0u || count[2] == 0u)
            {
                return demosaic_detail::demosaicFailure(
                    ErrorCode::IncompatibleArguments, kSuperPixelPlanOperation,
                    "a block of this mosaic holds no site of one of the three filters, so a super-pixel "
                    "would report a colour it never measured");
            }
        }
    }
    return Status::success();
}

Result<CfaChannelResult> superPixelCfa(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                       const ImageLayout& sourceLayout, const CfaDescriptor& cfa, void* destination,
                                       const ImageLayout& destinationLayout, const SpatialOutputPolicy& output,
                                       const CfaChannelSelection& selection)
{
    const Status planned = planCfaSuperPixel(cfa);
    if (planned.failed())
    {
        return planned;
    }
    const Status checked = demosaic_detail::checkImagePair(execution, sampleType, source, sourceLayout, 1u, destination,
                                                           destinationLayout, 3u, kSuperPixelOperation);
    if (checked.failed())
    {
        return checked;
    }
    if (!isOutputPrecisionContract(output.contract))
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kSuperPixelOperation,
                                                "the output precision contract is not one this build knows");
    }
    const Status conversionPolicy = checkConversionPolicy(output.writeBack, kSuperPixelOperation);
    if (conversionPolicy.failed())
    {
        return conversionPolicy;
    }
    if (spatialPrecisionError(sampleType, output.contract) != PrecisionError::None)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::Unsupported, kSuperPixelOperation,
                                                "this representation cannot keep the promise the output policy makes "
                                                "while the average is computed in Float32");
    }
    if (output.writeBack.domain != DomainMapping::CodeValue)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kSuperPixelOperation,
                                                "a super-pixel writes the numbers it measured, so its write back maps "
                                                "code values");
    }
    const std::uint32_t blockSize = cfaSuperPixelBlockSize(cfa.pattern());
    const BlockGeometry geometry =
        blockGeometryOf(blockSize, sourceLayout.extent().width(), sourceLayout.extent().height());
    if (destinationLayout.extent().width() != geometry.blockColumns ||
        destinationLayout.extent().height() != geometry.blockRows)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kSuperPixelOperation,
                                                "the destination is not the mosaic divided by its block size");
    }

    const Status sourceMask =
        demosaic_detail::checkSelectionMask(selection.validity, selection.mask, sourceLayout.extent().width(),
                                            sourceLayout.extent().height(), 1u, 0u, kSuperPixelOperation);
    if (sourceMask.failed())
    {
        return sourceMask;
    }
    const bool everySampleMeansSomething = selection.validity.rule() == ValidityRule::AllValid;
    if (everySampleMeansSomething == (selection.produced != nullptr))
    {
        return demosaic_detail::demosaicFailure(
            ErrorCode::IncompatibleArguments, kSuperPixelOperation,
            everySampleMeansSomething
                ? "an all-valid mosaic produces an all-valid super-pixel, so there is no mask to write"
                : "a super-pixel over incomplete data needs a produced mask for its combined answer");
    }
    const Status producedMask = demosaic_detail::checkProducedMask(selection.produced, geometry.blockColumns,
                                                                   geometry.blockRows, 1u, 3u, kSuperPixelOperation);
    if (producedMask.failed())
    {
        return producedMask;
    }
    const Status maskAliasing = demosaic_detail::checkMaskAliasing(source, sourceLayout.totalBytes(), destination,
                                                                   destinationLayout.totalBytes(), selection.mask,
                                                                   selection.produced, kSuperPixelOperation);
    if (maskAliasing.failed())
    {
        return maskAliasing;
    }
    if (selection.produced != nullptr)
    {
        selection.produced->fill(false);
    }

    const KernelValidityView validity{.descriptor = selection.validity, .mask = selection.mask, .maskPlane = 0u};
    std::atomic<std::uint64_t> meaningful{0u};
    std::atomic<std::uint64_t> convertedCount{0u};
    std::atomic<std::uint64_t> nonFiniteCount{0u};
    std::atomic<std::uint64_t> saturatedCount{0u};
    const std::uint64_t destinationSamples =
        static_cast<std::uint64_t>(geometry.blockColumns) * geometry.blockRows * 3u;
    const TaskCost cost = blockRowCost(geometry.blockColumns, blockSize * blockSize);
    Status dispatched = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        dispatched = parallelFor(execution, geometry.blockRows, cost, [&](TaskRange range) {
            std::uint64_t chunkValid = 0u;
            ConversionCounters localConversion;
            for (std::uint64_t blockRow = range.begin; blockRow < range.end; ++blockRow)
            {
                for (std::uint32_t blockColumn = 0u; blockColumn < geometry.blockColumns; ++blockColumn)
                {
                    std::array<double, 3> sum{};
                    std::array<std::uint32_t, 3> count{};
                    bool wholeBlockMeansSomething = true;
                    for (std::uint32_t withinRow = 0u; withinRow < blockSize; ++withinRow)
                    {
                        const std::uint64_t y = blockRow * blockSize + withinRow;
                        const auto* const sourceRow = reinterpret_cast<const SampleT*>(
                            static_cast<const std::uint8_t*>(source) +
                            demosaic_detail::channelRowByteOffset(sourceLayout, 0u, y));
                        for (std::uint32_t withinColumn = 0u; withinColumn < blockSize; ++withinColumn)
                        {
                            const std::uint32_t x = blockColumn * blockSize + withinColumn;
                            const SampleT value =
                                sourceRow[static_cast<std::size_t>(x) * sourceLayout.samplesPerPixel()];
                            if (!everySampleMeansSomething &&
                                (!sampleIsValidByValue(selection.validity, value) ||
                                 !plane_detail::maskAllows(validity, x, static_cast<std::uint32_t>(y))))
                            {
                                wholeBlockMeansSomething = false;
                            }
                            const std::size_t color =
                                static_cast<std::size_t>(cfa.colorAt(x, static_cast<std::uint32_t>(y)));
                            sum[color] += static_cast<double>(value);
                            ++count[color];
                        }
                    }
                    chunkValid += wholeBlockMeansSomething ? 1u : 0u;
                    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        auto* const destinationRow = reinterpret_cast<SampleT*>(
                            static_cast<std::uint8_t*>(destination) +
                            demosaic_detail::channelRowByteOffset(destinationLayout, channel, blockRow));
                        const float average = static_cast<float>(sum[channel] / static_cast<double>(count[channel]));
                        const ConvertedSample<SampleT> converted =
                            convertSample<float, SampleT>(average, output.writeBack);
                        if (converted.sourceWasNonFinite)
                        {
                            ++localConversion.nonFiniteInputs;
                            if (output.writeBack.nonFinite == NonFinitePolicy::Reject)
                            {
                                mergeConversionCounters(localConversion, convertedCount, nonFiniteCount,
                                                        saturatedCount);
                                const std::uint64_t position =
                                    (blockRow * geometry.blockColumns + blockColumn) * 3u + channel;
                                return superPixelConversionFailure(ErrorCode::InvalidData, position,
                                                                   destinationSamples);
                            }
                        }
                        if (converted.saturated)
                        {
                            ++localConversion.saturatedSamples;
                            if (output.writeBack.range == RangePolicy::Reject)
                            {
                                mergeConversionCounters(localConversion, convertedCount, nonFiniteCount,
                                                        saturatedCount);
                                const std::uint64_t position =
                                    (blockRow * geometry.blockColumns + blockColumn) * 3u + channel;
                                return superPixelConversionFailure(ErrorCode::OutOfRange, position, destinationSamples);
                            }
                        }
                        destinationRow[static_cast<std::size_t>(blockColumn) * destinationLayout.samplesPerPixel()] =
                            converted.value;
                        ++localConversion.samplesConverted;
                    }
                    if (selection.produced != nullptr)
                    {
                        for (std::uint32_t plane = 0u; plane < selection.produced->planeCount(); ++plane)
                        {
                            static_cast<void>(selection.produced->setValid(
                                plane, blockColumn, static_cast<std::uint32_t>(blockRow), wholeBlockMeansSomething));
                        }
                    }
                }
            }
            meaningful.fetch_add(chunkValid, std::memory_order_relaxed);
            mergeConversionCounters(localConversion, convertedCount, nonFiniteCount, saturatedCount);
            return Status::success();
        });
    });
    if (!known)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kSuperPixelOperation,
                                                "the representation is not one of the eight this engine stores");
    }
    if (dispatched.failed())
    {
        return dispatched;
    }
    if (selection.produced != nullptr)
    {
        selection.produced->clearPadBits();
    }

    CfaChannelResult result{};
    result.pattern = cfa.pattern();
    result.blockSize = blockSize;
    result.blockColumns = geometry.blockColumns;
    result.blockRows = geometry.blockRows;
    result.trailingColumns = geometry.trailingColumns;
    result.trailingRows = geometry.trailingRows;
    result.plannedWorkers = plannedWorkerCount(execution, geometry.blockRows, cost);
    const std::uint64_t blocks =
        static_cast<std::uint64_t>(geometry.blockColumns) * static_cast<std::uint64_t>(geometry.blockRows);
    result.run.selection.total = blocks;
    result.run.selection.valid = meaningful.load(std::memory_order_relaxed);
    result.run.outputsWritten = blocks;
    result.outputValidity = everySampleMeansSomething
                                ? ValidityDescriptor{}
                                : ValidityDescriptor::create({.rule = ValidityRule::ExplicitMask}).value();
    result.conversion.samplesConverted = convertedCount.load(std::memory_order_relaxed);
    result.conversion.nonFiniteInputs = nonFiniteCount.load(std::memory_order_relaxed);
    result.conversion.saturatedSamples = saturatedCount.load(std::memory_order_relaxed);
    return result;
}

} // namespace an
