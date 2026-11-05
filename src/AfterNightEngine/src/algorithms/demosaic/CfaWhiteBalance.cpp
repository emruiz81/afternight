// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/demosaic/DemosaicInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

// A mosaic's white balance and its stretch to full scale.
//
// Resolution is arithmetic over a calibration record and allocates nothing. The measurement walks
// square blocks from the top-left pixel exactly as the camera raw library's automatic white balance
// walks its grey box, and sums them into chunks whose boundaries depend on the work alone, so the
// answer is the same on every thread count. The multiplication builds a table of the filter
// multipliers along each row once, so its inner loop is a lookup, a product and a stated write-back.

namespace an
{
namespace
{

constexpr const char* kGreyWorldOperation = "an::measureCfaGreyWorld";
constexpr const char* kClipLevelsOperation = "an::cfaGreyWorldClipLevels";
constexpr const char* kResolveOperation = "an::resolveCfaWhiteBalance";
constexpr const char* kGainsOperation = "an::applyCfaChannelGains";
constexpr std::uint64_t kFixedGainScale = std::uint64_t{1u} << 24u;
constexpr std::uint64_t kMaximumExactGainNumerator = std::uint64_t{1u} << 37u;

/// Whether all three values are positive and finite, which is what a usable multiplier set is.
///
/// @param values the three multipliers.
/// @return true when each can scale a filter.
[[nodiscard]] bool usableGains(const CfaChannelValues& values) noexcept
{
    const auto usable = [](double value) noexcept { return std::isfinite(value) && value > 0.0; };
    return usable(values.red) && usable(values.green) && usable(values.blue);
}

/// The value a set holds for one filter colour.
///
/// @param values the set.
/// @param colour the filter.
/// @return that filter's value.
[[nodiscard]] double valueOf(const CfaChannelValues& values, CfaColor colour) noexcept
{
    switch (colour)
    {
    case CfaColor::Red:
        return values.red;
    case CfaColor::Green:
        return values.green;
    case CfaColor::Blue:
        return values.blue;
    }
    return values.green;
}

/// Converts a non-negative gain to an exact Q24 numerator when every UInt16 product stays exact in double.
///
/// @param gain checked finite gain.
/// @param numerator receives the exact numerator on success.
/// @return true when fixed-point multiplication reproduces double multiplication and truncation.
[[nodiscard]] bool fixedGainNumerator(double gain, std::uint64_t& numerator) noexcept
{
    const double scaled = gain * static_cast<double>(kFixedGainScale);
    if (!std::isfinite(scaled) || scaled < 0.0 || scaled > static_cast<double>(kMaximumExactGainNumerator) ||
        std::trunc(scaled) != scaled)
    {
        return false;
    }
    numerator = static_cast<std::uint64_t>(scaled);
    return true;
}

/// The fixed numerator a set holds for one filter colour.
///
/// @param values red, green and blue Q24 numerators.
/// @param colour the filter.
/// @return that filter's numerator.
[[nodiscard]] std::uint64_t valueOf(const std::array<std::uint64_t, 3u>& values, CfaColor colour) noexcept
{
    switch (colour)
    {
    case CfaColor::Red:
        return values[0u];
    case CfaColor::Green:
        return values[1u];
    case CfaColor::Blue:
        return values[2u];
    }
    return values[1u];
}

/// The black a camera raw library leaves a mosaic's white level measured from.
///
/// The record's black level, plus the smallest of its four channel offsets, plus the smallest entry of
/// its black tile when it states one - the parts the library folds into its common black before it
/// measures the white level's span.
///
/// @param camera the calibration record.
/// @return the common black, in sensor codes.
[[nodiscard]] double commonBlackOf(const RawCameraMetadata& camera) noexcept
{
    double black =
        static_cast<double>(camera.blackLevel) +
        static_cast<double>(*std::min_element(std::begin(camera.channelBlack), std::end(camera.channelBlack)));
    if (!camera.blackPattern.empty())
    {
        black += static_cast<double>(*std::min_element(camera.blackPattern.begin(), camera.blackPattern.end()));
    }
    return black;
}

/// A refusal carrying how far a write-back got.
///
/// @param code whether a non-finite or a range decision refused.
/// @param completed the sample position reached.
/// @param total how many samples the operation intended to write.
/// @return the failure.
[[nodiscard]] Status gainConversionFailure(ErrorCode code, std::uint64_t completed, std::uint64_t total)
{
    return Status::failure(
        code, StatusDetail{.message = code == ErrorCode::InvalidData
                                          ? "a mosaic product is not finite and write back refuses one"
                                          : "a mosaic product is outside the destination representation and write back "
                                            "refuses one",
                           .operation = kGainsOperation,
                           .domain = ErrorDomain::Algorithms,
                           .context = {StatusContextField::ofCount(ContextKey::Completed, completed),
                                       StatusContextField::ofCount(ContextKey::Total, total)}});
}

/// Whether an execution may run and one image describes a picture a measurement can read.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation.
/// @param samples the first byte.
/// @param layout where the samples sit.
/// @return success, or the first reason the call cannot proceed.
[[nodiscard]] Status checkReadableImage(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                        const ImageLayout& layout)
{
    if (!execution.valid())
    {
        return demosaic_detail::demosaicFailure(ErrorCode::NotInitialized, kGreyWorldOperation,
                                                "a measurement needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (samples == nullptr || !isSampleType(sampleType) ||
        layout.elementSizeBytes() != sampleTypeSizeBytes(sampleType) ||
        reinterpret_cast<std::uintptr_t>(samples) % sampleTypeSizeBytes(sampleType) != 0u)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kGreyWorldOperation,
                                                "the samples, their representation and their layout do not describe "
                                                "one image");
    }
    if (layout.extent().channelCount() != 1u)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kGreyWorldOperation,
                                                "a mosaic has one channel");
    }
    return Status::success();
}

/// One chunk's share of a grey-world measurement.
struct GreyWorldPartial
{
    /// Summed samples, in red, green, blue order.
    std::array<double, 3> sums{};

    /// Counted samples, in the same order.
    std::array<std::uint64_t, 3> counts{};

    /// Blocks whose samples were added.
    std::uint64_t measured = 0u;

    /// Blocks left out for a clipped or non-finite sample.
    std::uint64_t skipped = 0u;
};

/// Measures the blocks of a range of block rows into one partial.
///
/// @tparam SampleT the representation.
/// @param samples the first byte of the mosaic.
/// @param layout where the samples sit.
/// @param cfa the mosaic.
/// @param request the block size and the clip levels.
/// @param selection which samples mean something.
/// @param firstBlockRow the first block row of the range.
/// @param endBlockRow one past the last.
/// @param partial where the range's sums are added.
template <typename SampleT>
void measureBlockRows(const void* samples, const ImageLayout& layout, const CfaDescriptor& cfa,
                      const CfaGreyWorldRequest& request, const CfaChannelSelection& selection,
                      std::uint64_t firstBlockRow, std::uint64_t endBlockRow, GreyWorldPartial& partial) noexcept
{
    const std::uint64_t width = layout.extent().width();
    const std::uint64_t height = layout.extent().height();
    const std::uint64_t blockSize = request.blockSize;
    const bool allValid = selection.validity.rule() == ValidityRule::AllValid;
    const KernelValidityView validity{.descriptor = selection.validity, .mask = selection.mask, .maskPlane = 0u};
    const std::array<double, 3> clip{request.clippedAbove.red, request.clippedAbove.green, request.clippedAbove.blue};
    const std::uint32_t step = layout.samplesPerPixel();
    for (std::uint64_t blockRow = firstBlockRow; blockRow < endBlockRow; ++blockRow)
    {
        const std::uint64_t top = blockRow * blockSize;
        const std::uint64_t bottom = (std::min)(top + blockSize, height);
        for (std::uint64_t left = 0u; left < width; left += blockSize)
        {
            const std::uint64_t right = (std::min)(left + blockSize, width);
            std::array<double, 3> sums{};
            std::array<std::uint64_t, 3> counts{};
            bool clipped = false;
            for (std::uint64_t y = top; y < bottom && !clipped; ++y)
            {
                const auto* const row = reinterpret_cast<const SampleT*>(
                    static_cast<const std::uint8_t*>(samples) + demosaic_detail::channelRowByteOffset(layout, 0u, y));
                for (std::uint64_t x = left; x < right; ++x)
                {
                    const SampleT value = row[static_cast<std::size_t>(x) * step];
                    if (!allValid && (!sampleIsValidByValue(selection.validity, value) ||
                                      !plane_detail::maskAllows(validity, static_cast<std::uint32_t>(x),
                                                                static_cast<std::uint32_t>(y))))
                    {
                        continue;
                    }
                    const auto colour = static_cast<std::size_t>(
                        cfa.colorAt(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
                    const auto widened = static_cast<double>(value);
                    if (!std::isfinite(widened) || widened > clip[colour])
                    {
                        clipped = true;
                        break;
                    }
                    sums[colour] += widened;
                    ++counts[colour];
                }
            }
            if (clipped)
            {
                ++partial.skipped;
                continue;
            }
            ++partial.measured;
            for (std::size_t colour = 0u; colour < 3u; ++colour)
            {
                partial.sums[colour] += sums[colour];
                partial.counts[colour] += counts[colour];
            }
        }
    }
}

} // namespace

Result<CfaGreyWorld> measureCfaGreyWorld(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                         const ImageLayout& layout, const CfaDescriptor& cfa,
                                         const CfaGreyWorldRequest& request, const CfaChannelSelection& selection)
{
    if (!cfa.isMosaiced())
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kGreyWorldOperation,
                                                "only a mosaic has filters to measure a grey world under");
    }
    if (request.blockSize == 0u || std::isnan(request.clippedAbove.red) || std::isnan(request.clippedAbove.green) ||
        std::isnan(request.clippedAbove.blue))
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kGreyWorldOperation,
                                                "a grey-world block holds at least one pixel and every clip level is a "
                                                "number");
    }
    const Status readable = checkReadableImage(execution, sampleType, samples, layout);
    if (readable.failed())
    {
        return readable;
    }
    if (selection.produced != nullptr)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kGreyWorldOperation,
                                                "a measurement writes no image, so it produces no mask");
    }
    const Status sourceMask =
        demosaic_detail::checkSelectionMask(selection.validity, selection.mask, layout.extent().width(),
                                            layout.extent().height(), 1u, 0u, kGreyWorldOperation);
    if (sourceMask.failed())
    {
        return sourceMask;
    }

    const std::uint64_t width = layout.extent().width();
    const std::uint64_t height = layout.extent().height();
    const std::uint64_t blockRows = (height + request.blockSize - 1u) / request.blockSize;
    const std::uint64_t rowsPerBlockRow = (std::min)(static_cast<std::uint64_t>(request.blockSize), height);
    const TaskCost blockRowCost{.bytesPerItem = width * layout.elementSizeBytes() * rowsPerBlockRow,
                                .operationsPerItem = width * rowsPerBlockRow * 4u};
    const std::uint64_t chunks = deterministicChunkCount(blockRows, blockRowCost);
    if (chunks == 0u)
    {
        return CfaGreyWorld{};
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, chunks * sizeof(GreyWorldPartial), kDefaultBufferAlignmentBytes, kGreyWorldOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const partials = static_cast<GreyWorldPartial*>(memory->data());
    std::uninitialized_value_construct_n(partials, static_cast<std::size_t>(chunks));

    const std::uint64_t blockRowsPerChunk = (blockRows + chunks - 1u) / chunks;
    const TaskCost chunkCost{.bytesPerItem = blockRowCost.bytesPerItem * blockRowsPerChunk,
                             .operationsPerItem = blockRowCost.operationsPerItem * blockRowsPerChunk};
    Status ran = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        ran = parallelFor(execution, chunks, chunkCost, [&](TaskRange range) {
            for (std::uint64_t chunk = range.begin; chunk < range.end; ++chunk)
            {
                const Status stopped = execution.checkCancellation();
                if (stopped.failed())
                {
                    return stopped;
                }
                const std::uint64_t first = blockRows * chunk / chunks;
                const std::uint64_t end = blockRows * (chunk + 1u) / chunks;
                measureBlockRows<SampleT>(samples, layout, cfa, request, selection, first, end, partials[chunk]);
            }
            return Status::success();
        });
    });
    if (!known)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kGreyWorldOperation,
                                                "the representation is not one of the eight this engine stores");
    }
    if (ran.failed())
    {
        return ran;
    }

    CfaGreyWorld answer;
    for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
    {
        const GreyWorldPartial& partial = partials[chunk];
        answer.sums.red += partial.sums[0];
        answer.sums.green += partial.sums[1];
        answer.sums.blue += partial.sums[2];
        answer.redSamples += partial.counts[0];
        answer.greenSamples += partial.counts[1];
        answer.blueSamples += partial.counts[2];
        answer.blocksMeasured += partial.measured;
        answer.blocksSkipped += partial.skipped;
    }
    return answer;
}

Result<CfaChannelValues> cfaGreyWorldClipLevels(const RawCameraMetadata& camera, double margin)
{
    if (!std::isfinite(margin) || margin < 0.0)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kClipLevelsOperation,
                                                "a clip margin is a finite number of codes, not negative");
    }
    if (camera.whiteLevel == 0u)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidData, kClipLevelsOperation,
                                                "the calibration record states no white level");
    }
    const auto level = [&](std::size_t channel) noexcept {
        return static_cast<double>(camera.whiteLevel) - margin - static_cast<double>(camera.blackLevel) -
               static_cast<double>(camera.channelBlack[channel]);
    };
    const CfaChannelValues levels{level(0u), level(1u), level(2u)};
    if (!(levels.red > 0.0 && levels.green > 0.0 && levels.blue > 0.0))
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidData, kClipLevelsOperation,
                                                "a filter's clip level would not be above zero");
    }
    return levels;
}

Result<CfaWhiteBalance> resolveCfaWhiteBalance(const RawCameraMetadata& camera, const CfaWhiteBalanceRequest& request)
{
    if (!isCfaWhiteBalanceMode(request.mode))
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kResolveOperation,
                                                "the white-balance mode is outside its enumeration");
    }
    if (request.fullScaleCode.has_value() && !(std::isfinite(*request.fullScaleCode) && *request.fullScaleCode > 0.0))
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kResolveOperation,
                                                "a full scale is a positive finite code");
    }
    const CfaChannelValues daylight{camera.daylightMultipliers[0], camera.daylightMultipliers[1],
                                    camera.daylightMultipliers[2]};
    const bool daylightUsable = usableGains(daylight);
    CfaWhiteBalance answer;
    CfaChannelValues gains{};
    switch (request.mode)
    {
    case CfaWhiteBalanceMode::None:
        answer.source = CfaWhiteBalanceSource::Equal;
        break;
    case CfaWhiteBalanceMode::Daylight:
        if (!daylightUsable)
        {
            return demosaic_detail::demosaicFailure(ErrorCode::InvalidData, kResolveOperation,
                                                    "the calibration record states no usable daylight multipliers");
        }
        gains = daylight;
        answer.source = CfaWhiteBalanceSource::Daylight;
        break;
    case CfaWhiteBalanceMode::Camera: {
        const CfaChannelValues asShot{camera.cameraMultipliers[0], camera.cameraMultipliers[1],
                                      camera.cameraMultipliers[2]};
        if (usableGains(asShot))
        {
            gains = asShot;
            answer.source = CfaWhiteBalanceSource::CameraAsShot;
        }
        else if (daylightUsable)
        {
            gains = daylight;
            answer.source = CfaWhiteBalanceSource::Daylight;
        }
        else
        {
            return demosaic_detail::demosaicFailure(ErrorCode::InvalidData, kResolveOperation,
                                                    "the calibration record states neither as-shot nor daylight "
                                                    "multipliers");
        }
        break;
    }
    case CfaWhiteBalanceMode::Automatic: {
        if (request.measured == nullptr)
        {
            return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kResolveOperation,
                                                    "an automatic white balance needs a grey-world measurement");
        }
        const CfaGreyWorld& measured = *request.measured;
        // A filter's gain is the reciprocal of its mean. One the measurement saw nothing of keeps the
        // daylight multiplier, which is what the camera raw library leaves in place.
        const auto measuredGain = [](double sum, std::uint64_t count) noexcept {
            return count != 0u && std::isfinite(sum) && sum > 0.0 ? static_cast<double>(count) / sum : 0.0;
        };
        const auto gainFor = [&](double sum, std::uint64_t count, double fallback) noexcept {
            const double gain = measuredGain(sum, count);
            return gain > 0.0 ? gain : fallback;
        };
        gains = CfaChannelValues{gainFor(measured.sums.red, measured.redSamples, daylight.red),
                                 gainFor(measured.sums.green, measured.greenSamples, daylight.green),
                                 gainFor(measured.sums.blue, measured.blueSamples, daylight.blue)};
        if (!usableGains(gains))
        {
            return demosaic_detail::demosaicFailure(ErrorCode::InvalidData, kResolveOperation,
                                                    "the measurement and the calibration record leave a filter "
                                                    "without a gain");
        }
        answer.source = CfaWhiteBalanceSource::Measured;
        break;
    }
    case CfaWhiteBalanceMode::Custom:
        if (!usableGains(request.custom))
        {
            return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kResolveOperation,
                                                    "custom white-balance gains are positive and finite");
        }
        gains = request.custom;
        answer.source = CfaWhiteBalanceSource::Stated;
        break;
    }
    const double smallest = (std::min)({gains.red, gains.green, gains.blue});
    answer.gains = CfaChannelValues{gains.red / smallest, gains.green / smallest, gains.blue / smallest};
    if (request.fullScaleCode.has_value())
    {
        const double span = static_cast<double>(camera.whiteLevel) - commonBlackOf(camera);
        if (camera.whiteLevel == 0u || !(span > 0.0))
        {
            return demosaic_detail::demosaicFailure(ErrorCode::InvalidData, kResolveOperation,
                                                    "a stretch needs a white level above the common black");
        }
        answer.fullScale = *request.fullScaleCode / span;
    }
    return answer;
}

Result<CfaChannelResult> applyCfaChannelGains(const ExecutionContext& execution, SampleType sampleType,
                                              const void* source, const ImageLayout& sourceLayout,
                                              const CfaDescriptor& cfa, void* destination,
                                              const ImageLayout& destinationLayout, const CfaChannelValues& multipliers,
                                              const ConversionPolicy& writeBack, const CfaChannelSelection& selection)
{
    if (!cfa.isMosaiced())
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kGainsOperation,
                                                "only a mosaic has a filter to take a multiplier from");
    }
    const auto multiplierUsable = [](double value) noexcept { return std::isfinite(value) && value >= 0.0; };
    if (!multiplierUsable(multipliers.red) || !multiplierUsable(multipliers.green) ||
        !multiplierUsable(multipliers.blue))
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kGainsOperation,
                                                "a filter multiplier is finite and not negative");
    }
    const Status policy = checkConversionPolicy(writeBack, kGainsOperation);
    if (policy.failed())
    {
        return policy;
    }
    if (writeBack.domain != DomainMapping::CodeValue)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kGainsOperation,
                                                "the products are the numbers to store, so the write back maps code "
                                                "values");
    }
    const Status checked = demosaic_detail::checkImagePair(execution, sampleType, source, sourceLayout, 1u, destination,
                                                           destinationLayout, 1u, kGainsOperation);
    if (checked.failed())
    {
        return checked;
    }
    if (sourceLayout.extent() != destinationLayout.extent())
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kGainsOperation,
                                                "a multiplied mosaic has its source's extent");
    }
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const Status sourceMask =
        demosaic_detail::checkSelectionMask(selection.validity, selection.mask, width, height, 1u, 0u, kGainsOperation);
    if (sourceMask.failed())
    {
        return sourceMask;
    }
    if (selection.produced != nullptr)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kGainsOperation,
                                                "a multiplication preserves its source's validity, so it produces no "
                                                "mask");
    }
    if (selection.validity.rule() == ValidityRule::DomainRange)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kGainsOperation,
                                                "a stated range describes the unscaled numbers and would not describe "
                                                "the products");
    }
    if (selection.validity.rule() == ValidityRule::ZeroIsNoData &&
        (std::min)({multipliers.red, multipliers.green, multipliers.blue}) < 1.0)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::IncompatibleArguments, kGainsOperation,
                                                "a multiplier below one could turn a nonzero code into the no-data "
                                                "zero");
    }
    const Status maskAliasing =
        demosaic_detail::checkMaskAliasing(source, sourceLayout.totalBytes(), destination,
                                           destinationLayout.totalBytes(), selection.mask, nullptr, kGainsOperation);
    if (maskAliasing.failed())
    {
        return maskAliasing;
    }

    const bool allValid = selection.validity.rule() == ValidityRule::AllValid;
    const KernelValidityView validity{.descriptor = selection.validity, .mask = selection.mask, .maskPlane = 0u};
    const std::uint32_t period = cfa.hasFilterTable() ? kCfaTableSide : 2u;
    std::array<std::uint64_t, 3u> fixedGains{};
    const bool hasFixedGains = fixedGainNumerator(multipliers.red, fixedGains[0u]) &&
                               fixedGainNumerator(multipliers.green, fixedGains[1u]) &&
                               fixedGainNumerator(multipliers.blue, fixedGains[2u]);
    const std::uint64_t total = static_cast<std::uint64_t>(width) * height;
    const TaskCost cost = demosaic_detail::transferRowCost(width);
    std::atomic<std::uint64_t> meaningful{0u};
    std::atomic<std::uint64_t> convertedCount{0u};
    std::atomic<std::uint64_t> nonFiniteCount{0u};
    std::atomic<std::uint64_t> saturatedCount{0u};
    const auto merge = [&](const ConversionCounters& local) noexcept {
        convertedCount.fetch_add(local.samplesConverted, std::memory_order_relaxed);
        nonFiniteCount.fetch_add(local.nonFiniteInputs, std::memory_order_relaxed);
        saturatedCount.fetch_add(local.saturatedSamples, std::memory_order_relaxed);
    };
    Status ran = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        ran = parallelFor(execution, height, cost, [&](TaskRange range) {
            std::uint64_t chunkValid = 0u;
            ConversionCounters local;
            std::array<double, kCfaTableSide> rowMultipliers{};
            const std::uint32_t sourceStep = sourceLayout.samplesPerPixel();
            const std::uint32_t destinationStep = destinationLayout.samplesPerPixel();

            // Camera-RAW compatibility is the common integer path: its source cannot be
            // non-finite, its multipliers were checked above, and truncating a clamped
            // non-negative code is exactly the TowardZero conversion. Keeping that closed case
            // outside the general loop avoids asking the complete scalar conversion matrix five
            // policy questions per mosaic sample while retaining the same unconditional counts.
            if constexpr (std::is_same_v<SampleT, std::uint16_t>)
            {
                if (allValid && writeBack.rounding == RoundingMode::TowardZero &&
                    writeBack.range == RangePolicy::Saturate)
                {
                    if (hasFixedGains)
                    {
                        constexpr std::uint64_t kMaximumScaledCode =
                            static_cast<std::uint64_t>((std::numeric_limits<std::uint16_t>::max)()) * kFixedGainScale;
                        std::array<std::uint64_t, kCfaTableSide> rowFixedGains{};
                        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                        {
                            const auto y = static_cast<std::uint32_t>(rowIndex);
                            for (std::uint32_t phase = 0u; phase < period; ++phase)
                                rowFixedGains[phase] = valueOf(fixedGains, cfa.colorAt(phase, y));

                            const auto* const input = reinterpret_cast<const std::uint16_t*>(
                                static_cast<const std::uint8_t*>(source) +
                                demosaic_detail::channelRowByteOffset(sourceLayout, 0u, rowIndex));
                            auto* const output = reinterpret_cast<std::uint16_t*>(
                                static_cast<std::uint8_t*>(destination) +
                                demosaic_detail::channelRowByteOffset(destinationLayout, 0u, rowIndex));
                            std::uint32_t phase = 0u;
                            for (std::uint32_t x = 0u; x < width; ++x)
                            {
                                const std::uint64_t scaled =
                                    static_cast<std::uint64_t>(input[static_cast<std::size_t>(x) * sourceStep]) *
                                    rowFixedGains[phase];
                                phase = phase + 1u == period ? 0u : phase + 1u;
                                const bool saturated = scaled > kMaximumScaledCode;
                                output[static_cast<std::size_t>(x) * destinationStep] =
                                    saturated ? (std::numeric_limits<std::uint16_t>::max)()
                                              : static_cast<std::uint16_t>(scaled / kFixedGainScale);
                                local.saturatedSamples += saturated ? 1u : 0u;
                            }
                        }
                        local.samplesConverted = (range.end - range.begin) * width;
                        merge(local);
                        return Status::success();
                    }

                    constexpr double kMaximumCode = static_cast<double>((std::numeric_limits<std::uint16_t>::max)());
                    for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                    {
                        const auto y = static_cast<std::uint32_t>(rowIndex);
                        for (std::uint32_t phase = 0u; phase < period; ++phase)
                            rowMultipliers[phase] = valueOf(multipliers, cfa.colorAt(phase, y));

                        const auto* const input = reinterpret_cast<const std::uint16_t*>(
                            static_cast<const std::uint8_t*>(source) +
                            demosaic_detail::channelRowByteOffset(sourceLayout, 0u, rowIndex));
                        auto* const output = reinterpret_cast<std::uint16_t*>(
                            static_cast<std::uint8_t*>(destination) +
                            demosaic_detail::channelRowByteOffset(destinationLayout, 0u, rowIndex));
                        std::uint32_t phase = 0u;
                        for (std::uint32_t x = 0u; x < width; ++x)
                        {
                            const double product =
                                static_cast<double>(input[static_cast<std::size_t>(x) * sourceStep]) *
                                rowMultipliers[phase];
                            phase = phase + 1u == period ? 0u : phase + 1u;
                            const bool saturated = product > kMaximumCode;
                            output[static_cast<std::size_t>(x) * destinationStep] =
                                saturated ? (std::numeric_limits<std::uint16_t>::max)()
                                          : static_cast<std::uint16_t>(product);
                            local.saturatedSamples += saturated ? 1u : 0u;
                        }
                    }
                    local.samplesConverted = (range.end - range.begin) * width;
                    merge(local);
                    return Status::success();
                }
            }

            for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
            {
                const auto y = static_cast<std::uint32_t>(rowIndex);
                for (std::uint32_t phase = 0u; phase < period; ++phase)
                {
                    rowMultipliers[phase] = valueOf(multipliers, cfa.colorAt(phase, y));
                }
                const auto* const input =
                    reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(source) +
                                                     demosaic_detail::channelRowByteOffset(sourceLayout, 0u, rowIndex));
                auto* const output =
                    reinterpret_cast<SampleT*>(static_cast<std::uint8_t*>(destination) +
                                               demosaic_detail::channelRowByteOffset(destinationLayout, 0u, rowIndex));
                std::uint32_t phase = 0u;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const SampleT value = input[static_cast<std::size_t>(x) * sourceStep];
                    const double product = static_cast<double>(value) * rowMultipliers[phase];
                    phase = phase + 1u == period ? 0u : phase + 1u;
                    const ConvertedSample<SampleT> stored = convertSample<double, SampleT>(product, writeBack);
                    if (stored.sourceWasNonFinite)
                    {
                        ++local.nonFiniteInputs;
                        if (writeBack.nonFinite == NonFinitePolicy::Reject)
                        {
                            merge(local);
                            return gainConversionFailure(ErrorCode::InvalidData, rowIndex * width + x, total);
                        }
                    }
                    if (stored.saturated)
                    {
                        ++local.saturatedSamples;
                        if (writeBack.range == RangePolicy::Reject)
                        {
                            merge(local);
                            return gainConversionFailure(ErrorCode::OutOfRange, rowIndex * width + x, total);
                        }
                    }
                    output[static_cast<std::size_t>(x) * destinationStep] = stored.value;
                    ++local.samplesConverted;
                    if (!allValid)
                    {
                        chunkValid +=
                            sampleIsValidByValue(selection.validity, value) && plane_detail::maskAllows(validity, x, y)
                                ? 1u
                                : 0u;
                    }
                }
            }
            meaningful.fetch_add(chunkValid, std::memory_order_relaxed);
            merge(local);
            return Status::success();
        });
    });
    if (!known)
    {
        return demosaic_detail::demosaicFailure(ErrorCode::InvalidArgument, kGainsOperation,
                                                "the representation is not one of the eight this engine stores");
    }
    if (ran.failed())
    {
        return ran;
    }

    CfaChannelResult result{};
    result.pattern = cfa.pattern();
    result.plannedWorkers = plannedWorkerCount(execution, height, cost);
    result.run.selection.total = total;
    result.run.selection.valid = allValid ? total : meaningful.load(std::memory_order_relaxed);
    result.run.outputsWritten = total;
    result.outputValidity = selection.validity;
    result.conversion.samplesConverted = convertedCount.load(std::memory_order_relaxed);
    result.conversion.nonFiniteInputs = nonFiniteCount.load(std::memory_order_relaxed);
    result.conversion.saturatedSamples = saturatedCount.load(std::memory_order_relaxed);
    return result;
}

} // namespace an
