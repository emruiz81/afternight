// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the operation half of the validity model.

#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>

// The mask operations, all of which are one walk over rows and none of which reads a sample's
// meaning twice.
//
// **The unit of work is one row of one bit-plane.** Splitting a mask by byte would put two threads
// in the same byte, which for a bit-packed buffer is a lost update rather than a slow path; splitting
// it by row cannot, because a row's bytes belong to it alone and the padding between rows belongs to
// nobody. It is also what keeps the operations from having to know whether the mask is contiguous.
//
// **The vector body and the scalar body are both here.** A table entry that is null is the scalar
// table's answer, and the loop beside every dispatch call is what runs then. That is what makes
// forcing `ComputeBackend::Reference` a comparison rather than a relabelling of the same code.
//
// **A threshold resolves to a small closed set before any sample is read.** The relation, its bounds
// and the non-finite answer are validated once and become three values a body compares against, so
// there is no callable, no branch on an enumeration inside the loop that the compiler cannot see
// through, and nothing per sample that was not decided per operation.

namespace an
{
namespace
{

constexpr const char* kBuildOperation = "an::buildImageMask";
constexpr const char* kMarkOperation = "an::markImageValidity";
constexpr const char* kCombineOperation = "an::combineImageMasks";
constexpr const char* kInvertOperation = "an::invertImageMask";
constexpr const char* kCountOperation = "an::countImageMask";
constexpr const char* kApplyOperation = "an::applyImageMask";
constexpr const char* kBinarizeOperation = "an::binarizeImage";
constexpr const char* kResolveOperation = "an::resolveImageValidity";

/// The failure an operation that holds nothing produces.
///
/// @param operation the engine operation to name.
/// @return `ErrorCode::NotInitialized`.
[[nodiscard]] Status notRunnable(const char* operation)
{
    return Status::failure(ErrorCode::NotInitialized,
                           StatusDetail{.message = "an operation that holds nothing decides nothing",
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms});
}

/// The failure a wrong argument produces.
///
/// @param operation the engine operation to name.
/// @param code which failure.
/// @param message the sentence explaining what was wrong.
/// @param parameter which argument was wrong.
/// @return the failure, naming the argument.
[[nodiscard]] Status maskFailure(const char* operation, ErrorCode code, const char* message, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Whether an operation may run at all, and whether its backend selection succeeded.
///
/// @param execution the operation.
/// @param operation the engine operation to name in a failure.
/// @return success, or the reason it may not run.
[[nodiscard]] Status readyToRun(const ExecutionContext& execution, const char* operation)
{
    if (!execution.valid())
    {
        return notRunnable(operation);
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    return Status::success();
}

/// A threshold reduced to what a loop compares against.
///
/// The relation is resolved once, so the body branches on a small integer rather than re-reading an
/// optional and an enumeration per sample.
struct ResolvedThreshold
{
    /// Which relation decides.
    MaskPredicate predicate = MaskPredicate::AtOrAbove;

    /// The lower bound, or the interval's start.
    double lowerBound = 0.0;

    /// The interval's end, equal to `lowerBound` for the single-bound relations.
    double upperBound = 0.0;

    /// What a sample that is not a finite number does.
    bool nonFiniteSelected = false;
};

/// Whether a threshold describes a comparison, and if so what it reduces to.
///
/// @param threshold the request's relation and bounds.
/// @param operation the engine operation to name in a failure.
/// @param resolved where the reduced form is written on success.
/// @return success, or `ErrorCode::InvalidArgument` naming what was wrong.
[[nodiscard]] Status resolveThreshold(const SampleThreshold& threshold, const char* operation,
                                      ResolvedThreshold& resolved)
{
    if (!isMaskPredicate(threshold.predicate))
    {
        return maskFailure(operation, ErrorCode::InvalidArgument,
                           "this is not a relation this build compares samples with", "threshold");
    }
    if (!std::isfinite(threshold.lowerBound))
    {
        return maskFailure(operation, ErrorCode::InvalidArgument, "a threshold bound must be a finite number",
                           "threshold");
    }
    const bool needsUpper = maskPredicateNeedsUpperBound(threshold.predicate);
    if (needsUpper != threshold.upperBound.has_value())
    {
        return maskFailure(operation, ErrorCode::InvalidArgument,
                           needsUpper ? "a range relation compares against two bounds and was given one"
                                      : "a single-bound relation has no second bound to compare against",
                           "threshold");
    }
    if (needsUpper)
    {
        if (!std::isfinite(*threshold.upperBound))
        {
            return maskFailure(operation, ErrorCode::InvalidArgument, "a threshold bound must be a finite number",
                               "threshold");
        }
        if (*threshold.upperBound < threshold.lowerBound)
        {
            return maskFailure(operation, ErrorCode::InvalidArgument, "a closed interval ends at or after it starts",
                               "threshold");
        }
    }
    resolved.predicate = threshold.predicate;
    resolved.lowerBound = threshold.lowerBound;
    resolved.upperBound = needsUpper ? *threshold.upperBound : threshold.lowerBound;
    resolved.nonFiniteSelected = threshold.nonFiniteSelected;
    return Status::success();
}

/// Whether one value satisfies a resolved threshold.
///
/// @param threshold the reduced relation and bounds.
/// @param value the sample, widened.
/// @return true when the relation selects it.
[[nodiscard]] bool thresholdSelects(const ResolvedThreshold& threshold, double value) noexcept
{
    if (!std::isfinite(value))
    {
        return threshold.nonFiniteSelected;
    }
    switch (threshold.predicate)
    {
    case MaskPredicate::Below:
        return value < threshold.lowerBound;
    case MaskPredicate::AtOrBelow:
        return value <= threshold.lowerBound;
    case MaskPredicate::AtOrAbove:
        return value >= threshold.lowerBound;
    case MaskPredicate::Above:
        return value > threshold.lowerBound;
    case MaskPredicate::InsideClosedRange:
        return value >= threshold.lowerBound && value <= threshold.upperBound;
    case MaskPredicate::OutsideClosedRange:
        return value < threshold.lowerBound || value > threshold.upperBound;
    }
    return false;
}

/// Where one channel's first sample of one row sits, in bytes from the buffer's first.
///
/// @param layout the geometry the channel belongs to.
/// @param channelIndex which channel, below the layout's channel count.
/// @param y the row.
/// @return the byte offset.
[[nodiscard]] std::uint64_t channelRowByteOffset(const ImageLayout& layout, std::uint32_t channelIndex,
                                                 std::uint64_t y) noexcept
{
    const std::uint32_t plane = channelIndex / layout.samplesPerPixel();
    const std::uint32_t within = channelIndex % layout.samplesPerPixel();
    return static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
           static_cast<std::uint64_t>(within) * static_cast<std::uint64_t>(layout.elementSizeBytes());
}

/// Whether a layout describes samples of a stated representation.
///
/// @param sampleType the representation.
/// @param layout the geometry.
/// @return true when the layout's element size is that representation's.
[[nodiscard]] bool describesSamples(SampleType sampleType, const ImageLayout& layout) noexcept
{
    return isSampleType(sampleType) && sampleTypeSizeBytes(sampleType) == layout.elementSizeBytes();
}

/// One sample of one channel of one row, widened.
///
/// @tparam SampleT the resident representation.
/// @param base the row's first byte for that channel.
/// @param step how many samples separate one pixel from the next in that channel.
/// @param x the column.
/// @return the sample as a double.
template <typename SampleT>
[[nodiscard]] double widenedSample(const std::uint8_t* base, std::uint64_t step, std::uint32_t x) noexcept
{
    return static_cast<double>(reinterpret_cast<const SampleT*>(base)[x * step]);
}

/// How many items a walk over every row of every bit-plane of a mask covers.
///
/// @param mask the mask.
/// @return the plane count times the height.
[[nodiscard]] std::uint64_t maskRowCount(const ValidityMaskView<const std::uint8_t>& mask) noexcept
{
    return static_cast<std::uint64_t>(mask.planeCount()) * static_cast<std::uint64_t>(mask.height());
}

/// What a mask combine and a mask inversion cost per row, for the grain policy.
///
/// @param mask the mask being written.
/// @param readsPerByte how many bytes are touched per byte written.
/// @return the cost of one row.
[[nodiscard]] TaskCost maskRowCost(const ValidityMaskView<const std::uint8_t>& mask,
                                   std::uint64_t readsPerByte) noexcept
{
    const std::uint64_t bytes = mask.minimumRowBytes();
    return TaskCost{.bytesPerItem = bytes * readsPerByte, .operationsPerItem = bytes};
}

/// Whether two masks describe the same shape.
///
/// @param left one mask.
/// @param right the other.
/// @return true when their extents agree.
[[nodiscard]] bool sameMaskShape(const ValidityMaskView<const std::uint8_t>& left,
                                 const ValidityMaskView<const std::uint8_t>& right) noexcept
{
    return left.extent() == right.extent();
}

/// Whether a destination mask and a source mask are either the same memory or disjoint memory.
///
/// @param destination the mask written.
/// @param source the mask read.
/// @return true when a write cannot read bytes another item has already changed.
[[nodiscard]] bool aliasSafe(const ValidityMaskView<std::uint8_t>& destination,
                             const ValidityMaskView<const std::uint8_t>& source) noexcept
{
    if (destination.data() == source.data() && destination.rowStrideBytes() == source.rowStrideBytes() &&
        destination.planeStrideBytes() == source.planeStrideBytes())
    {
        return true;
    }
    return !regionsOverlap(destination.data(), destination.spannedByteCount(), source.data(),
                           source.spannedByteCount());
}

/// Whether a mask and an image share memory.
///
/// @param mask the mask.
/// @param samples the image's first byte.
/// @param layout where the image's samples sit.
/// @return true when the two share at least one byte.
[[nodiscard]] bool maskTouchesImage(const ValidityMaskView<const std::uint8_t>& mask, const void* samples,
                                    const ImageLayout& layout) noexcept
{
    return regionsOverlap(mask.data(), mask.spannedByteCount(), static_cast<const std::uint8_t*>(samples),
                          layout.totalBytes());
}

/// Writes one row of a mask from a per-pixel answer.
///
/// @tparam DecideT a callable taking a column and returning whether the pixel is selected.
/// @param bytes the row's own bytes, whose last byte's spare bits are left at zero.
/// @param width how many pixels the row holds.
/// @param decide what to write.
template <typename DecideT>
void writeMaskRow(std::span<std::uint8_t> bytes, std::uint32_t width, DecideT decide) noexcept
{
    for (std::size_t index = 0u; index < bytes.size(); ++index)
    {
        const std::uint32_t base = static_cast<std::uint32_t>(index) * 8u;
        const std::uint32_t count = (std::min)(8u, width - base);
        std::uint8_t value = 0u;
        for (std::uint32_t bit = 0u; bit < count; ++bit)
        {
            if (decide(base + bit))
            {
                value = static_cast<std::uint8_t>(value | static_cast<std::uint8_t>(1u << bit));
            }
        }
        bytes[index] = value;
    }
}

/// Whether one pixel of one channel of an image satisfies a rule, for every representation.
///
/// A function template selected once per operation rather than a callable consulted per sample: the
/// representation is a compile-time parameter of the row body below, and this is how the run-time
/// `SampleType` reaches it.
///
/// @tparam SampleT the resident representation.
/// @param mask where the answers go.
/// @param source the image's first byte.
/// @param layout where the image's samples sit.
/// @param threshold the reduced relation and bounds.
/// @param anyChannel whether a shared bit is set where any channel is selected rather than where
///        every one is.
/// @param planeIndex which bit-plane of the mask.
/// @param y which row.
template <typename SampleT>
void thresholdMaskRow(const ValidityMaskView<std::uint8_t>& mask, const std::uint8_t* source, const ImageLayout& layout,
                      const ResolvedThreshold& threshold, bool anyChannel, std::uint32_t planeIndex,
                      std::uint32_t y) noexcept
{
    const std::uint32_t channelCount = layout.extent().channelCount();
    const std::uint64_t step = static_cast<std::uint64_t>(layout.samplesPerPixel());
    const bool shared = mask.isShared();
    const std::span<std::uint8_t> bytes = *mask.row(planeIndex, y);

    if (!shared || channelCount == 1u)
    {
        const std::uint32_t channel = shared ? 0u : planeIndex;
        const std::uint8_t* const base = source + channelRowByteOffset(layout, channel, y);
        writeMaskRow(bytes, mask.width(), [&](std::uint32_t x) noexcept {
            return thresholdSelects(threshold, widenedSample<SampleT>(base, step, x));
        });
        return;
    }

    writeMaskRow(bytes, mask.width(), [&](std::uint32_t x) noexcept {
        bool selected = !anyChannel;
        for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
        {
            const std::uint8_t* const base = source + channelRowByteOffset(layout, channel, y);
            const bool one = thresholdSelects(threshold, widenedSample<SampleT>(base, step, x));
            selected = anyChannel ? (selected || one) : (selected && one);
        }
        return selected;
    });
}

/// Whether one pixel of one channel means something under a validity rule, for every representation.
///
/// @tparam SampleT the resident representation.
/// @param mask where the answers go.
/// @param source the image's first byte.
/// @param layout where the image's samples sit.
/// @param validity the rule to judge the samples by.
/// @param planeIndex which bit-plane of the mask.
/// @param y which row.
template <typename SampleT>
void validityMaskRow(const ValidityMaskView<std::uint8_t>& mask, const std::uint8_t* source, const ImageLayout& layout,
                     const ValidityDescriptor& validity, std::uint32_t planeIndex, std::uint32_t y) noexcept
{
    const std::uint32_t channelCount = layout.extent().channelCount();
    const std::uint64_t step = static_cast<std::uint64_t>(layout.samplesPerPixel());
    const bool shared = mask.isShared();
    const std::span<std::uint8_t> bytes = *mask.row(planeIndex, y);

    if (!shared || channelCount == 1u)
    {
        const std::uint32_t channel = shared ? 0u : planeIndex;
        const std::uint8_t* const base = source + channelRowByteOffset(layout, channel, y);
        writeMaskRow(bytes, mask.width(), [&](std::uint32_t x) noexcept {
            return sampleIsValidByValue(validity, reinterpret_cast<const SampleT*>(base)[x * step]);
        });
        return;
    }

    writeMaskRow(bytes, mask.width(), [&](std::uint32_t x) noexcept {
        for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
        {
            const std::uint8_t* const base = source + channelRowByteOffset(layout, channel, y);
            if (!sampleIsValidByValue(validity, reinterpret_cast<const SampleT*>(base)[x * step]))
            {
                return false;
            }
        }
        return true;
    });
}

/// Writes one row of a binarize.
///
/// @tparam SampleT the resident representation.
/// @param source the source image's first byte.
/// @param sourceLayout where the source samples sit.
/// @param destination the destination image's first byte.
/// @param destinationLayout where the destination samples sit.
/// @param threshold the reduced relation and bounds.
/// @param selectedValue the already converted sample a selected source becomes.
/// @param unselectedValue the already converted sample an unselected source becomes.
/// @param y which row.
/// @return how many samples of the row were selected.
template <typename SampleT>
[[nodiscard]] std::uint64_t binarizeRow(const std::uint8_t* source, const ImageLayout& sourceLayout,
                                        std::uint8_t* destination, const ImageLayout& destinationLayout,
                                        const ResolvedThreshold& threshold, SampleT selectedValue,
                                        SampleT unselectedValue, std::uint32_t y) noexcept
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t channelCount = sourceLayout.extent().channelCount();
    const std::uint64_t sourceStep = static_cast<std::uint64_t>(sourceLayout.samplesPerPixel());
    const std::uint64_t destinationStep = static_cast<std::uint64_t>(destinationLayout.samplesPerPixel());
    std::uint64_t selected = 0u;
    for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
    {
        const SampleT* const in =
            reinterpret_cast<const SampleT*>(source + channelRowByteOffset(sourceLayout, channel, y));
        SampleT* const out =
            reinterpret_cast<SampleT*>(destination + channelRowByteOffset(destinationLayout, channel, y));
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const bool hit = thresholdSelects(threshold, static_cast<double>(in[x * sourceStep]));
            out[x * destinationStep] = hit ? selectedValue : unselectedValue;
            selected += hit ? 1u : 0u;
        }
    }
    return selected;
}

/// Writes one row of an apply.
///
/// @tparam SampleT the resident representation.
/// @param samples the image's first byte.
/// @param layout where the image's samples sit.
/// @param mask which samples mean something.
/// @param invalidValue the already converted sample a meaningless one becomes.
/// @param y which row.
/// @return how many samples of the row were replaced.
template <typename SampleT>
[[nodiscard]] std::uint64_t applyRow(std::uint8_t* samples, const ImageLayout& layout,
                                     const ValidityMaskView<const std::uint8_t>& mask, SampleT invalidValue,
                                     std::uint32_t y) noexcept
{
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t channelCount = layout.extent().channelCount();
    const std::uint64_t step = static_cast<std::uint64_t>(layout.samplesPerPixel());
    const bool shared = mask.isShared();
    std::uint64_t replaced = 0u;
    for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
    {
        const std::span<const std::uint8_t> bits = *mask.row(shared ? 0u : channel, y);
        SampleT* const out = reinterpret_cast<SampleT*>(samples + channelRowByteOffset(layout, channel, y));
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const bool meaningful = (bits[x / 8u] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
            if (!meaningful)
            {
                out[x * step] = invalidValue;
                ++replaced;
            }
        }
    }
    return replaced;
}

/// Converts one stated value into the image's representation, once.
///
/// @tparam SampleT the resident representation.
/// @param value the value to write.
/// @param policy how it becomes a sample.
/// @param saturated set when the representation could not hold the value exactly.
/// @return the converted sample.
template <typename SampleT>
[[nodiscard]] SampleT convertedConstant(double value, const ConversionPolicy& policy, bool& saturated) noexcept
{
    const ConvertedSample<SampleT> converted = convertSample<double, SampleT>(value, policy);
    saturated = converted.saturated;
    return converted.value;
}

/// Whether one input of a validity transition says a pixel means something.
///
/// @param input the input and what it claims.
/// @param sharedDestination whether the answer is one bit for every channel.
/// @param planeIndex which bit-plane of the destination mask.
/// @param x the column.
/// @param y the row.
/// @return true when this input alone does not make the pixel meaningless.
[[nodiscard]] bool inputAnswers(const MaskedValidityInput& input, bool sharedDestination, std::uint32_t planeIndex,
                                std::uint32_t x, std::uint32_t y) noexcept
{
    if (input.validity.rule() == ValidityRule::AllValid)
    {
        return true;
    }
    if (input.validity.rule() == ValidityRule::ExplicitMask)
    {
        const ValidityMaskView<const std::uint8_t>& mask = *input.mask;
        if (mask.isShared())
        {
            return mask.test(0u, x, y).value_or(false);
        }
        if (!sharedDestination)
        {
            return mask.test(planeIndex, x, y).value_or(false);
        }
        for (std::uint32_t plane = 0u; plane < mask.planeCount(); ++plane)
        {
            if (!mask.test(plane, x, y).value_or(false))
            {
                return false;
            }
        }
        return true;
    }

    const ImageLayout& layout = *input.layout;
    const std::uint32_t channelCount = layout.extent().channelCount();
    const std::uint64_t step = static_cast<std::uint64_t>(layout.samplesPerPixel());
    const std::uint8_t* const samples = static_cast<const std::uint8_t*>(input.samples);
    const std::uint32_t firstChannel = sharedDestination ? 0u : planeIndex;
    const std::uint32_t lastChannel = sharedDestination ? channelCount : firstChannel + 1u;

    bool meaningful = true;
    const bool ran = dispatchSampleType(input.sampleType, [&]<typename SampleT>() noexcept {
        for (std::uint32_t channel = firstChannel; channel < lastChannel && meaningful; ++channel)
        {
            const std::uint8_t* const base = samples + channelRowByteOffset(layout, channel, y);
            meaningful = sampleIsValidByValue(input.validity, reinterpret_cast<const SampleT*>(base)[x * step]);
        }
    });
    return ran && meaningful;
}

} // namespace

const char* maskPredicateName(MaskPredicate predicate) noexcept
{
    switch (predicate)
    {
    case MaskPredicate::Below:
        return "Below";
    case MaskPredicate::AtOrBelow:
        return "AtOrBelow";
    case MaskPredicate::AtOrAbove:
        return "AtOrAbove";
    case MaskPredicate::Above:
        return "Above";
    case MaskPredicate::InsideClosedRange:
        return "InsideClosedRange";
    case MaskPredicate::OutsideClosedRange:
        return "OutsideClosedRange";
    }
    return "UnknownMaskPredicate";
}

Status buildImageMask(const ExecutionContext& execution, SampleType sampleType, const void* source,
                      const ImageLayout& sourceLayout, const MaskThresholdRequest& request,
                      const ValidityMaskView<std::uint8_t>& mask)
{
    const Status ready = readyToRun(execution, kBuildOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (source == nullptr)
    {
        return maskFailure(kBuildOperation, ErrorCode::InvalidArgument,
                           "this argument does not describe samples this engine can read", "source");
    }
    if (!describesSamples(sampleType, sourceLayout))
    {
        return maskFailure(kBuildOperation, ErrorCode::InvalidArgument,
                           "this layout does not describe the stated representation", "sourceLayout");
    }
    ResolvedThreshold threshold{};
    const Status resolved = resolveThreshold(request.threshold, kBuildOperation, threshold);
    if (resolved.failed())
    {
        return resolved;
    }
    const Status covers = checkMaskCovers(mask.constView(), sourceLayout.extent(), kBuildOperation);
    if (covers.failed())
    {
        return covers;
    }
    const bool needsRule = mask.isShared() && sourceLayout.extent().channelCount() > 1u;
    if (needsRule != request.channelRule.has_value())
    {
        return maskFailure(kBuildOperation, ErrorCode::InvalidArgument,
                           needsRule ? "a mask shared by several channels needs a rule saying how they combine"
                                     : "this mask answers for one channel at a time, so it has no combination rule",
                           "request");
    }
    if (needsRule && !isMaskChannelRule(*request.channelRule))
    {
        return maskFailure(kBuildOperation, ErrorCode::InvalidArgument, "this is not a channel rule this build knows",
                           "request");
    }
    if (maskTouchesImage(mask.constView(), source, sourceLayout))
    {
        return maskFailure(kBuildOperation, ErrorCode::IncompatibleArguments,
                           "a mask that shares memory with the image it judges would be reading bits it has just "
                           "written",
                           "mask");
    }

    const bool anyChannel = needsRule && *request.channelRule == MaskChannelRule::AnyChannel;
    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source);
    const std::uint32_t height = mask.height();
    const std::uint64_t channels = static_cast<std::uint64_t>(sourceLayout.extent().channelCount());
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(mask.width()) * channels *
                                        static_cast<std::uint64_t>(sourceLayout.elementSizeBytes()),
                        .operationsPerItem = static_cast<std::uint64_t>(mask.width()) * channels};

    Status outcome = Status::success();
    const bool ran = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        outcome = parallelFor(execution, maskRowCount(mask.constView()), cost, [&](TaskRange range) -> Status {
            for (std::uint64_t item = range.begin; item < range.end; ++item)
            {
                const std::uint32_t planeIndex = static_cast<std::uint32_t>(item / height);
                const std::uint32_t y = static_cast<std::uint32_t>(item % height);
                thresholdMaskRow<SampleT>(mask, sourceBytes, sourceLayout, threshold, anyChannel, planeIndex, y);
            }
            return Status::success();
        });
    });
    if (!ran)
    {
        return maskFailure(kBuildOperation, ErrorCode::InvalidArgument,
                           "this is not a representation this engine stores", "sampleType");
    }
    return outcome;
}

Status markImageValidity(const ExecutionContext& execution, SampleType sampleType, const void* source,
                         const ImageLayout& sourceLayout, const ValidityDescriptor& validity,
                         const ValidityMaskView<std::uint8_t>& mask)
{
    const Status ready = readyToRun(execution, kMarkOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (source == nullptr)
    {
        return maskFailure(kMarkOperation, ErrorCode::InvalidArgument,
                           "this argument does not describe samples this engine can read", "source");
    }
    if (!describesSamples(sampleType, sourceLayout))
    {
        return maskFailure(kMarkOperation, ErrorCode::InvalidArgument,
                           "this layout does not describe the stated representation", "sourceLayout");
    }
    if (!validityRuleIsDecidedByValue(validity.rule()))
    {
        return maskFailure(kMarkOperation, ErrorCode::InvalidArgument,
                           "a mask cannot be built from a rule that says a mask decides", "validity");
    }
    const Status covers = checkMaskCovers(mask.constView(), sourceLayout.extent(), kMarkOperation);
    if (covers.failed())
    {
        return covers;
    }
    if (maskTouchesImage(mask.constView(), source, sourceLayout))
    {
        return maskFailure(kMarkOperation, ErrorCode::IncompatibleArguments,
                           "a mask that shares memory with the image it judges would be reading bits it has just "
                           "written",
                           "mask");
    }

    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source);
    const std::uint32_t height = mask.height();
    const std::uint64_t channels = static_cast<std::uint64_t>(sourceLayout.extent().channelCount());
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(mask.width()) * channels *
                                        static_cast<std::uint64_t>(sourceLayout.elementSizeBytes()),
                        .operationsPerItem = static_cast<std::uint64_t>(mask.width()) * channels};

    Status outcome = Status::success();
    const bool ran = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        outcome = parallelFor(execution, maskRowCount(mask.constView()), cost, [&](TaskRange range) -> Status {
            for (std::uint64_t item = range.begin; item < range.end; ++item)
            {
                const std::uint32_t planeIndex = static_cast<std::uint32_t>(item / height);
                const std::uint32_t y = static_cast<std::uint32_t>(item % height);
                validityMaskRow<SampleT>(mask, sourceBytes, sourceLayout, validity, planeIndex, y);
            }
            return Status::success();
        });
    });
    if (!ran)
    {
        return maskFailure(kMarkOperation, ErrorCode::InvalidArgument,
                           "this is not a representation this engine stores", "sampleType");
    }
    return outcome;
}

Status combineImageMasks(const ExecutionContext& execution, const ValidityMaskView<std::uint8_t>& destination,
                         const ValidityMaskView<const std::uint8_t>& left,
                         const ValidityMaskView<const std::uint8_t>& right, ValidityCombination combination)
{
    const Status ready = readyToRun(execution, kCombineOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (!isValidityCombination(combination))
    {
        return maskFailure(kCombineOperation, ErrorCode::InvalidArgument, "this is not a combination this build knows",
                           "combination");
    }
    if (!sameMaskShape(destination.constView(), left) || !sameMaskShape(destination.constView(), right))
    {
        return maskFailure(kCombineOperation, ErrorCode::IncompatibleArguments,
                           "a combination answers for one picture, so all three masks are the same shape",
                           "destination");
    }
    if (!aliasSafe(destination, left) || !aliasSafe(destination, right))
    {
        return maskFailure(kCombineOperation, ErrorCode::InvalidArgument,
                           "a destination that partly shares memory with a source would read bytes the same call "
                           "has already written",
                           "destination");
    }

    const MaskCombineRunFn compiled =
        combination == ValidityCombination::Intersection ? maskIntersectRunFor(execution) : maskUnionRunFor(execution);
    const bool intersect = combination == ValidityCombination::Intersection;
    const std::uint32_t height = destination.height();
    const std::uint8_t lastByteMask = packedMaskLastByteMask(destination.width());

    return parallelFor(execution, maskRowCount(destination.constView()), maskRowCost(destination.constView(), 3u),
                       [&](TaskRange range) -> Status {
                           for (std::uint64_t item = range.begin; item < range.end; ++item)
                           {
                               const std::uint32_t planeIndex = static_cast<std::uint32_t>(item / height);
                               const std::uint32_t y = static_cast<std::uint32_t>(item % height);
                               const std::span<std::uint8_t> out = *destination.row(planeIndex, y);
                               const std::span<const std::uint8_t> a = *left.row(planeIndex, y);
                               const std::span<const std::uint8_t> b = *right.row(planeIndex, y);
                               if (compiled != nullptr)
                               {
                                   compiled(a.data(), b.data(), out.data(), static_cast<std::uint64_t>(out.size()));
                               }
                               else
                               {
                                   for (std::size_t index = 0u; index < out.size(); ++index)
                                   {
                                       out[index] = intersect ? static_cast<std::uint8_t>(a[index] & b[index])
                                                              : static_cast<std::uint8_t>(a[index] | b[index]);
                                   }
                               }
                               out.back() = static_cast<std::uint8_t>(out.back() & lastByteMask);
                           }
                           return Status::success();
                       });
}

Status invertImageMask(const ExecutionContext& execution, const ValidityMaskView<std::uint8_t>& destination,
                       const ValidityMaskView<const std::uint8_t>& source)
{
    const Status ready = readyToRun(execution, kInvertOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (!sameMaskShape(destination.constView(), source))
    {
        return maskFailure(kInvertOperation, ErrorCode::IncompatibleArguments,
                           "an inversion answers for one picture, so both masks are the same shape", "destination");
    }
    if (!aliasSafe(destination, source))
    {
        return maskFailure(kInvertOperation, ErrorCode::InvalidArgument,
                           "a destination that partly shares memory with the source would read bytes the same call "
                           "has already written",
                           "destination");
    }

    const MaskInvertRunFn compiled = maskInvertRunFor(execution);
    const std::uint32_t height = destination.height();
    const std::uint8_t lastByteMask = packedMaskLastByteMask(destination.width());

    return parallelFor(execution, maskRowCount(destination.constView()), maskRowCost(destination.constView(), 2u),
                       [&](TaskRange range) -> Status {
                           for (std::uint64_t item = range.begin; item < range.end; ++item)
                           {
                               const std::uint32_t planeIndex = static_cast<std::uint32_t>(item / height);
                               const std::uint32_t y = static_cast<std::uint32_t>(item % height);
                               const std::span<std::uint8_t> out = *destination.row(planeIndex, y);
                               const std::span<const std::uint8_t> in = *source.row(planeIndex, y);
                               if (compiled != nullptr)
                               {
                                   compiled(in.data(), out.data(), static_cast<std::uint64_t>(out.size()));
                               }
                               else
                               {
                                   for (std::size_t index = 0u; index < out.size(); ++index)
                                   {
                                       out[index] = static_cast<std::uint8_t>(~in[index]);
                                   }
                               }
                               out.back() = static_cast<std::uint8_t>(out.back() & lastByteMask);
                           }
                           return Status::success();
                       });
}

Result<ValidityCount> countImageMask(const ExecutionContext& execution,
                                     const ValidityMaskView<const std::uint8_t>& mask)
{
    const Status ready = readyToRun(execution, kCountOperation);
    if (ready.failed())
    {
        return ready;
    }

    const MaskPopcountRunFn compiled = maskPopcountRunFor(execution);
    const std::uint32_t height = mask.height();
    const std::uint8_t lastByteMask = packedMaskLastByteMask(mask.width());
    std::atomic<std::uint64_t> total{0u};

    const Status walked =
        parallelFor(execution, maskRowCount(mask), maskRowCost(mask, 1u), [&](TaskRange range) -> Status {
            std::uint64_t local = 0u;
            for (std::uint64_t item = range.begin; item < range.end; ++item)
            {
                const std::uint32_t planeIndex = static_cast<std::uint32_t>(item / height);
                const std::uint32_t y = static_cast<std::uint32_t>(item % height);
                const std::span<const std::uint8_t> bits = *mask.row(planeIndex, y);
                const std::size_t whole = bits.size() - 1u;
                if (compiled != nullptr)
                {
                    local += compiled(bits.data(), static_cast<std::uint64_t>(whole));
                }
                else
                {
                    for (std::size_t index = 0u; index < whole; ++index)
                    {
                        local += static_cast<std::uint64_t>(std::popcount(bits[index]));
                    }
                }
                local +=
                    static_cast<std::uint64_t>(std::popcount(static_cast<std::uint8_t>(bits[whole] & lastByteMask)));
            }
            total.fetch_add(local, std::memory_order_relaxed);
            return Status::success();
        });
    if (walked.failed())
    {
        return walked;
    }
    return ValidityCount{.valid = total.load(std::memory_order_relaxed), .total = mask.bitCount()};
}

Result<MaskApplyResult> applyImageMask(const ExecutionContext& execution, SampleType sampleType, void* samples,
                                       const ImageLayout& layout, const ValidityMaskView<const std::uint8_t>& mask,
                                       double invalidValue, const ConversionPolicy& conversion)
{
    const Status ready = readyToRun(execution, kApplyOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (samples == nullptr)
    {
        return maskFailure(kApplyOperation, ErrorCode::InvalidArgument,
                           "this argument does not describe samples this engine can write", "samples");
    }
    if (!describesSamples(sampleType, layout))
    {
        return maskFailure(kApplyOperation, ErrorCode::InvalidArgument,
                           "this layout does not describe the stated representation", "layout");
    }
    if (!std::isfinite(invalidValue))
    {
        return maskFailure(kApplyOperation, ErrorCode::InvalidArgument,
                           "the value substituted for a meaningless sample must be a finite number", "invalidValue");
    }
    const Status policyKnown = checkConversionPolicy(conversion, kApplyOperation);
    if (policyKnown.failed())
    {
        return policyKnown;
    }
    const Status covers = checkMaskCovers(mask, layout.extent(), kApplyOperation);
    if (covers.failed())
    {
        return covers;
    }
    if (maskTouchesImage(mask, samples, layout))
    {
        return maskFailure(kApplyOperation, ErrorCode::IncompatibleArguments,
                           "a mask that shares memory with the image it is applied to would be read after the same "
                           "call has written over it",
                           "mask");
    }

    std::uint8_t* const imageBytes = static_cast<std::uint8_t*>(samples);
    const std::uint32_t height = layout.extent().height();
    const std::uint64_t channels = static_cast<std::uint64_t>(layout.extent().channelCount());
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(layout.extent().width()) * channels *
                                        static_cast<std::uint64_t>(layout.elementSizeBytes()) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(layout.extent().width()) * channels};

    bool saturated = false;
    std::atomic<std::uint64_t> replaced{0u};
    Status outcome = Status::success();
    const bool ran = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        const SampleT written = convertedConstant<SampleT>(invalidValue, conversion, saturated);
        outcome = parallelFor(execution, static_cast<std::uint64_t>(height), cost, [&](TaskRange range) -> Status {
            std::uint64_t local = 0u;
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                local += applyRow<SampleT>(imageBytes, layout, mask, written, static_cast<std::uint32_t>(y));
            }
            replaced.fetch_add(local, std::memory_order_relaxed);
            return Status::success();
        });
    });
    if (!ran)
    {
        return maskFailure(kApplyOperation, ErrorCode::InvalidArgument,
                           "this is not a representation this engine stores", "sampleType");
    }
    if (outcome.failed())
    {
        return outcome;
    }
    return MaskApplyResult{.replaced = replaced.load(std::memory_order_relaxed),
                           .total = layout.extent().sampleCount(),
                           .saturatedValue = saturated};
}

Result<BinarizeResult> binarizeImage(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                     const ImageLayout& sourceLayout, const BinarizeRequest& request, void* destination,
                                     const ImageLayout& destinationLayout)
{
    const Status ready = readyToRun(execution, kBinarizeOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (source == nullptr || destination == nullptr)
    {
        return maskFailure(kBinarizeOperation, ErrorCode::InvalidArgument,
                           "this argument does not describe samples this engine can move",
                           source == nullptr ? "source" : "destination");
    }
    if (!describesSamples(sampleType, sourceLayout))
    {
        return maskFailure(kBinarizeOperation, ErrorCode::InvalidArgument,
                           "this layout does not describe the stated representation", "sourceLayout");
    }
    if (!describesSamples(sampleType, destinationLayout))
    {
        return maskFailure(kBinarizeOperation, ErrorCode::InvalidArgument,
                           "this layout does not describe the stated representation", "destinationLayout");
    }
    ResolvedThreshold threshold{};
    const Status resolved = resolveThreshold(request.threshold, kBinarizeOperation, threshold);
    if (resolved.failed())
    {
        return resolved;
    }
    if (!std::isfinite(request.selectedValue) || !std::isfinite(request.unselectedValue))
    {
        return maskFailure(kBinarizeOperation, ErrorCode::InvalidArgument,
                           "the two values a binarize writes must both be finite numbers", "request");
    }
    const Status policyKnown = checkConversionPolicy(request.conversion, kBinarizeOperation);
    if (policyKnown.failed())
    {
        return policyKnown;
    }
    if (sourceLayout.extent() != destinationLayout.extent())
    {
        return maskFailure(kBinarizeOperation, ErrorCode::IncompatibleArguments,
                           "a binarize writes the picture it was given, so both sides are the same shape",
                           "destinationLayout");
    }
    const bool exactlyAliased = source == destination && sourceLayout == destinationLayout;
    if (!exactlyAliased && regionsOverlap(static_cast<const std::uint8_t*>(source), sourceLayout.totalBytes(),
                                          static_cast<std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return maskFailure(kBinarizeOperation, ErrorCode::IncompatibleArguments,
                           "a destination that partly shares memory with the source would make what a sample ends "
                           "up as depend on the order the rows happened to be scheduled in",
                           "destination");
    }

    const std::uint8_t* const sourceBytes = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const destinationBytes = static_cast<std::uint8_t*>(destination);
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint64_t channels = static_cast<std::uint64_t>(sourceLayout.extent().channelCount());
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(sourceLayout.extent().width()) * channels *
                                        static_cast<std::uint64_t>(sourceLayout.elementSizeBytes()) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(sourceLayout.extent().width()) * channels};

    std::uint32_t saturatedValues = 0u;
    std::atomic<std::uint64_t> selected{0u};
    Status outcome = Status::success();
    const bool ran = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        bool selectedSaturated = false;
        bool unselectedSaturated = false;
        const SampleT high = convertedConstant<SampleT>(request.selectedValue, request.conversion, selectedSaturated);
        const SampleT low =
            convertedConstant<SampleT>(request.unselectedValue, request.conversion, unselectedSaturated);
        saturatedValues = (selectedSaturated ? 1u : 0u) + (unselectedSaturated ? 1u : 0u);
        outcome = parallelFor(execution, static_cast<std::uint64_t>(height), cost, [&](TaskRange range) -> Status {
            std::uint64_t local = 0u;
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                local += binarizeRow<SampleT>(sourceBytes, sourceLayout, destinationBytes, destinationLayout, threshold,
                                              high, low, static_cast<std::uint32_t>(y));
            }
            selected.fetch_add(local, std::memory_order_relaxed);
            return Status::success();
        });
    });
    if (!ran)
    {
        return maskFailure(kBinarizeOperation, ErrorCode::InvalidArgument,
                           "this is not a representation this engine stores", "sampleType");
    }
    if (outcome.failed())
    {
        return outcome;
    }
    return BinarizeResult{.selected = selected.load(std::memory_order_relaxed),
                          .total = sourceLayout.extent().sampleCount(),
                          .saturatedValues = saturatedValues};
}

Result<ValidityDescriptor> resolveImageValidity(const ExecutionContext& execution, const ValidityTransition& transition,
                                                std::span<const MaskedValidityInput> inputs,
                                                std::optional<ValidityMaskView<std::uint8_t>> destinationMask)
{
    const Status ready = readyToRun(execution, kResolveOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (inputs.size() > static_cast<std::size_t>(kMaxValidityTransitionInputs))
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "a validity transition carries one operation's arguments, and this build "
                                    "materialises a combined mask over at most eight of them",
                         .operation = kResolveOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "inputs"),
                                     StatusContextField::ofCount(ContextKey::Requested,
                                                                 static_cast<std::uint64_t>(inputs.size())),
                                     StatusContextField::ofCount(ContextKey::Limit, kMaxValidityTransitionInputs)}});
    }

    std::array<ValidityDescriptor, kMaxValidityTransitionInputs> claims{};
    for (std::size_t index = 0u; index < inputs.size(); ++index)
    {
        claims[index] = inputs[index].validity;
    }
    const Result<ValidityDescriptor> answer =
        applyValidityPolicy(transition, std::span<const ValidityDescriptor>(claims.data(), inputs.size()));
    if (!answer.hasValue())
    {
        return answer.status();
    }

    const bool needsMask = answer->needsMask();
    if (needsMask != destinationMask.has_value())
    {
        return maskFailure(kResolveOperation, ErrorCode::InvalidArgument,
                           needsMask ? "this transition answers with the mask rule, so it has to be given the mask "
                                       "to write"
                                     : "this transition answers with a rule that needs no mask, so there is nothing "
                                       "to write into one",
                           "destinationMask");
    }
    if (!needsMask)
    {
        return *answer;
    }

    const ValidityMaskView<std::uint8_t>& mask = *destinationMask;
    for (const MaskedValidityInput& input : inputs)
    {
        if (input.validity.rule() == ValidityRule::AllValid)
        {
            continue;
        }
        if (input.validity.rule() == ValidityRule::ExplicitMask)
        {
            if (!input.mask.has_value())
            {
                return maskFailure(kResolveOperation, ErrorCode::InvalidArgument,
                                   "an input whose rule says a mask decides has to be given that mask", "inputs");
            }
            if (input.mask->width() != mask.width() || input.mask->height() != mask.height())
            {
                return maskFailure(kResolveOperation, ErrorCode::IncompatibleArguments,
                                   "every input of a transition answers for the same picture", "inputs");
            }
            continue;
        }
        if (input.samples == nullptr || input.layout == nullptr)
        {
            return maskFailure(kResolveOperation, ErrorCode::InvalidArgument,
                               "an input whose rule is decided by value has to be given the samples it judges",
                               "inputs");
        }
        if (!describesSamples(input.sampleType, *input.layout))
        {
            return maskFailure(kResolveOperation, ErrorCode::InvalidArgument,
                               "this layout does not describe the stated representation", "inputs");
        }
        const Status covers = checkMaskCovers(mask.constView(), input.layout->extent(), kResolveOperation);
        if (covers.failed())
        {
            return covers;
        }
    }

    const bool shared = mask.isShared();
    const std::uint32_t height = mask.height();
    const std::uint32_t width = mask.width();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * inputs.size(),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * inputs.size()};

    const Status walked = parallelFor(execution, maskRowCount(mask.constView()), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t item = range.begin; item < range.end; ++item)
        {
            const std::uint32_t planeIndex = static_cast<std::uint32_t>(item / height);
            const std::uint32_t y = static_cast<std::uint32_t>(item % height);
            writeMaskRow(*mask.row(planeIndex, y), width, [&](std::uint32_t x) noexcept {
                for (const MaskedValidityInput& input : inputs)
                {
                    if (!inputAnswers(input, shared, planeIndex, x, y))
                    {
                        return false;
                    }
                }
                return true;
            });
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    return *answer;
}

} // namespace an
