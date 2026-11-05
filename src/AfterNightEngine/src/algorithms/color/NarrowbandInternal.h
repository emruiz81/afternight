// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - helpers shared by the narrowband extraction, palette, colour-map and
// combination operations.
//
// The four narrowband operations check their arguments, address their channels and hold their rank
// memory the same way, and share a handful of scalar formulas. Keeping that here means a validity
// rule, an overlap test or a midtones transfer cannot drift between them. Everything is inline
// because each operation's hot loop calls the addressing helpers per sample.
//
// Channel addressing follows the layout rather than an assumption about it: a channel lives in plane
// `channel / samplesPerPixel`, at offset `channel % samplesPerPixel` within an interleaved pixel. The
// addressing helpers assume the caller has already checked the coordinates, the channel and the
// element size; they validate nothing.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

/// Argument checks, channel addressing, scalar transfers and rank memory shared by the narrowband
/// operations.
namespace an::narrowband_detail
{

/// A failure in the algorithms domain naming the operation that refused.
///
/// @param operation the public operation's name, a string literal.
/// @param code which failure.
/// @param message the sentence explaining what was wrong.
/// @return the failed status.
[[nodiscard]] inline Status failure(const char* operation, ErrorCode code, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a representation is one the floating narrowband operations compute in.
///
/// @param sampleType the representation to test.
/// @return true for Float32 and Float64 only.
[[nodiscard]] inline bool supportedWorkingType(SampleType sampleType) noexcept
{
    return sampleType == SampleType::Float32 || sampleType == SampleType::Float64;
}

/// Whether a pointer can address samples of a representation.
///
/// @param samples the first byte of the buffer.
/// @param sampleType the representation it is read as.
/// @return true when the pointer is not null and is a multiple of the sample size.
[[nodiscard]] inline bool alignedFor(const void* samples, SampleType sampleType) noexcept
{
    return samples != nullptr && reinterpret_cast<std::uintptr_t>(samples) % sampleTypeSizeBytes(sampleType) == 0u;
}

/// Whether two layouts cover the same pixels, whatever their channel counts and strides.
///
/// @param left one layout.
/// @param right the other.
/// @return true when the widths and the heights are equal.
[[nodiscard]] inline bool sameGeometry(const ImageLayout& left, const ImageLayout& right) noexcept
{
    return left.extent().width() == right.extent().width() && left.extent().height() == right.extent().height();
}

/// Whether two buffers are the same image in the same arrangement, the one overlap an in-place
/// operation accepts.
///
/// @param left one buffer's first byte.
/// @param leftLayout its layout.
/// @param right the other buffer's first byte.
/// @param rightLayout its layout.
/// @return true when the pointers and the layouts are both equal.
[[nodiscard]] inline bool exactlyAliases(const void* left, const ImageLayout& leftLayout, const void* right,
                                         const ImageLayout& rightLayout) noexcept
{
    return left == right && leftLayout == rightLayout;
}

/// Whether the byte spans two layouts describe share any byte.
///
/// Conservative: two interleaved images whose samples never coincide still overlap when their spans
/// do.
///
/// @param left one buffer's first byte.
/// @param leftLayout its layout, whose total byte count is the span.
/// @param right the other buffer's first byte.
/// @param rightLayout its layout.
/// @return true when the spans intersect.
[[nodiscard]] inline bool overlaps(const void* left, const ImageLayout& leftLayout, const void* right,
                                   const ImageLayout& rightLayout) noexcept
{
    return regionsOverlap(static_cast<const std::uint8_t*>(left), leftLayout.totalBytes(),
                          static_cast<const std::uint8_t*>(right), rightLayout.totalBytes());
}

/// Whether the view's packed mask, if it has one, marks a pixel valid.
///
/// @param validity the view; without a mask every pixel is accepted.
/// @param x the column, inside the mask.
/// @param y the row, inside the mask.
/// @return true when there is no mask or the pixel's bit in the view's plane is set.
[[nodiscard]] inline bool maskAccepts(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
    const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

/// Whether one sample passes the view's value rule and its mask.
///
/// @tparam SampleT the resident representation.
/// @param validity the view.
/// @param value the sample, read at `x` and `y`.
/// @param x the column.
/// @param y the row.
/// @return true when both the value rule and the mask accept it.
template <typename SampleT>
[[nodiscard]] inline bool admits(const KernelValidityView& validity, SampleT value, std::uint32_t x,
                                 std::uint32_t y) noexcept
{
    return sampleIsValidByValue(validity.descriptor, value) && maskAccepts(validity, x, y);
}

/// Checks a validity view against the image it selects from and the buffer the operation writes.
///
/// @param validity the view.
/// @param sourceLayout the layout whose width and height the mask must cover.
/// @param destination the first writable byte, which the mask must not overlap.
/// @param destinationLayout its layout.
/// @param operation the public operation's name for a failure.
/// @return success; `ErrorCode::InvalidArgument` for a mask or a nonzero mask plane under a rule
///         that reads none, a missing mask under the explicit-mask rule, or a mask overlapping the
///         destination; or `ErrorCode::IncompatibleArguments` for a mask whose width, height or plane
///         count does not cover the source.
[[nodiscard]] inline Status validateValidity(const KernelValidityView& validity, const ImageLayout& sourceLayout,
                                             const void* destination, const ImageLayout& destinationLayout,
                                             const char* operation)
{
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return failure(operation, ErrorCode::InvalidArgument, "the validity view supplies an unused mask");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return failure(operation, ErrorCode::InvalidArgument, "an explicit-mask operation supplies no mask");
    }
    if (validity.mask->width() != sourceLayout.extent().width() ||
        validity.mask->height() != sourceLayout.extent().height() || validity.maskPlane >= validity.mask->planeCount())
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "the validity mask does not cover the narrowband source");
    }
    if (regionsOverlap(validity.mask->data(), validity.mask->spannedByteCount(),
                       static_cast<const std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return failure(operation, ErrorCode::InvalidArgument, "the validity mask overlaps the narrowband destination");
    }
    return Status::success();
}

/// The first sample of the plane row that holds a channel, for reading.
///
/// The channel's own offset inside an interleaved pixel is not applied; `sampleAt` and
/// `channelRowView` add it.
///
/// @tparam SampleT the resident representation.
/// @param samples the buffer's first byte.
/// @param layout its layout.
/// @param channel the channel, below the layout's channel count.
/// @param y the row, below the layout's height.
/// @return the row's first sample in the channel's plane.
template <typename SampleT>
[[nodiscard]] inline const SampleT* channelRow(const void* samples, const ImageLayout& layout, std::uint32_t channel,
                                               std::uint32_t y) noexcept
{
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(samples);
    return reinterpret_cast<const SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
}

/// The first sample of the plane row that holds a channel, for writing.
///
/// @tparam SampleT the resident representation.
/// @param samples the buffer's first byte.
/// @param layout its layout.
/// @param channel the channel, below the layout's channel count.
/// @param y the row, below the layout's height.
/// @return the row's first sample in the channel's plane.
template <typename SampleT>
[[nodiscard]] inline SampleT* channelRow(void* samples, const ImageLayout& layout, std::uint32_t channel,
                                         std::uint32_t y) noexcept
{
    std::uint8_t* bytes = static_cast<std::uint8_t*>(samples);
    return reinterpret_cast<SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
}

/// One channel of one pixel.
///
/// @tparam SampleT the resident representation.
/// @param samples the buffer's first byte.
/// @param layout its layout.
/// @param x the column, below the width.
/// @param y the row, below the height.
/// @param channel the channel, below the channel count.
/// @return the stored sample.
template <typename SampleT>
[[nodiscard]] inline SampleT sampleAt(const void* samples, const ImageLayout& layout, std::uint32_t x, std::uint32_t y,
                                      std::uint32_t channel) noexcept
{
    const SampleT* row = channelRow<SampleT>(samples, layout, channel, y);
    return row[static_cast<std::size_t>(x) * layout.samplesPerPixel() + channel % layout.samplesPerPixel()];
}

/// Stores one channel of one pixel, narrowing the value to the representation with a plain cast.
///
/// @tparam SampleT the resident representation.
/// @param samples the buffer's first byte.
/// @param layout its layout.
/// @param x the column, below the width.
/// @param y the row, below the height.
/// @param channel the channel, below the channel count.
/// @param value the value to store.
template <typename SampleT>
inline void writeAt(void* samples, const ImageLayout& layout, std::uint32_t x, std::uint32_t y, std::uint32_t channel,
                    double value) noexcept
{
    SampleT* row = channelRow<SampleT>(samples, layout, channel, y);
    row[static_cast<std::size_t>(x) * layout.samplesPerPixel() + channel % layout.samplesPerPixel()] =
        static_cast<SampleT>(value);
}

/// A sample brought into the unit interval, with a non-finite one read as zero and counted.
///
/// @param value the sample.
/// @param nonFinite incremented when `value` is not finite.
/// @return zero for a non-finite value, otherwise `value` clamped to `[0, 1]`.
[[nodiscard]] inline double finiteUnit(double value, std::uint64_t& nonFinite) noexcept
{
    if (!std::isfinite(value))
    {
        ++nonFinite;
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
}

/// The midtones transfer function with a shadows clip, as the colour mapper applies it.
///
/// A value at or below `shadows` maps to zero and one at or above one maps to one; between them the
/// value is renormalized to `(value - shadows) / (1 - shadows)` and passed through
/// `((balance - 1) x) / ((2 balance - 1) x - balance)`, which leaves `balance` itself at one half.
/// Finite for a balance strictly inside `(0, 1)`, where every caller keeps it.
///
/// @param value the sample; a non-finite one maps to zero.
/// @param balance the midtones balance.
/// @param shadows the black point, below one.
/// @return the transferred value.
[[nodiscard]] inline double midtones(double value, double balance, double shadows) noexcept
{
    if (!std::isfinite(value))
    {
        return 0.0;
    }
    if (value <= shadows)
    {
        return 0.0;
    }
    if (value >= 1.0)
    {
        return 1.0;
    }
    const double normalized = (value - shadows) / (1.0 - shadows);
    return ((balance - 1.0) * normalized) / ((2.0 * balance - 1.0) * normalized - balance);
}

/// The half of an HSV conversion that depends on hue and saturation alone.
///
/// A layer's hue and saturation are fixed and its value changes per pixel, so this half - a modulo,
/// a floor and three products - is resolved once per layer rather than for every pixel.
struct HsvWeights
{
    /// Whether the saturation is zero, which makes every channel the value itself.
    bool grey = true;

    /// Which sixth of the hue circle the hue lies in.
    std::uint32_t index = 0u;

    /// What the value multiplies for the channel the hue has least of.
    double p = 1.0;

    /// What the value multiplies for the falling channel.
    double q = 1.0;

    /// What the value multiplies for the rising channel.
    double t = 1.0;
};

/// Resolves the hue-and-saturation half of `hsv` once.
///
/// @param hueDegrees the hue, in degrees, any value.
/// @param saturation the saturation, in [0, 1].
/// @return the weights.
[[nodiscard]] inline HsvWeights hsvWeights(double hueDegrees, double saturation) noexcept
{
    HsvWeights weights;
    if (saturation <= std::numeric_limits<double>::epsilon())
    {
        return weights;
    }
    double hue = std::fmod(hueDegrees, 360.0);
    if (hue < 0.0)
    {
        hue += 360.0;
    }
    const double sector = hue / 60.0;
    const double fraction = sector - std::floor(sector);
    weights.grey = false;
    weights.index = static_cast<std::uint32_t>(sector) % 6u;
    weights.p = 1.0 - saturation;
    weights.q = 1.0 - saturation * fraction;
    weights.t = 1.0 - saturation * (1.0 - fraction);
    return weights;
}

/// An HSV colour at one value, from weights resolved once.
///
/// Each channel is the value times the weight `hsvWeights` resolved, which is the product `hsv`
/// forms, so the two give the same bits.
///
/// @param weights the hue-and-saturation half.
/// @param value the value.
/// @return red, green and blue.
[[nodiscard]] inline std::array<double, 3u> hsv(const HsvWeights& weights, double value) noexcept
{
    if (weights.grey)
    {
        return {value, value, value};
    }
    const double p = value * weights.p;
    const double q = value * weights.q;
    const double t = value * weights.t;
    switch (weights.index)
    {
    case 0u:
        return {value, t, p};
    case 1u:
        return {q, value, p};
    case 2u:
        return {p, value, t};
    case 3u:
        return {p, q, value};
    case 4u:
        return {t, p, value};
    default:
        return {value, p, q};
    }
}

/// An HSV colour.
///
/// @param hueDegrees the hue, in degrees, any value.
/// @param saturation the saturation, in [0, 1].
/// @param value the value.
/// @return red, green and blue.
[[nodiscard]] inline std::array<double, 3u> hsv(double hueDegrees, double saturation, double value) noexcept
{
    return hsv(hsvWeights(hueDegrees, saturation), value);
}

/// One channel of one row, as `sampleAt` and `writeAt` address it, resolved once for the row.
///
/// @tparam SampleT the resident representation, const to read.
template <typename SampleT> struct ChannelRowView
{
    /// The row's first sample of the channel.
    SampleT* first = nullptr;

    /// Samples between one pixel's and the next's.
    std::size_t step = 1u;

    /// The sample of pixel `x`.
    ///
    /// @param x the column.
    /// @return the sample.
    [[nodiscard]] SampleT& at(std::uint32_t x) const noexcept
    {
        return first[static_cast<std::size_t>(x) * step];
    }
};

/// One channel of one row of a buffer to read.
///
/// @param samples the buffer.
/// @param layout where its samples are.
/// @param channel which channel.
/// @param y which row.
/// @return the row, addressing the samples `sampleAt` would.
template <typename SampleT>
[[nodiscard]] inline ChannelRowView<const SampleT> channelRowView(const void* samples, const ImageLayout& layout,
                                                                  std::uint32_t channel, std::uint32_t y) noexcept
{
    return {channelRow<SampleT>(samples, layout, channel, y) + channel % layout.samplesPerPixel(),
            layout.samplesPerPixel()};
}

/// One channel of one row of a buffer to write.
///
/// @param samples the buffer.
/// @param layout where its samples are.
/// @param channel which channel.
/// @param y which row.
/// @return the row, addressing the samples `writeAt` would.
template <typename SampleT>
[[nodiscard]] inline ChannelRowView<SampleT> channelRowView(void* samples, const ImageLayout& layout,
                                                            std::uint32_t channel, std::uint32_t y) noexcept
{
    return {channelRow<SampleT>(samples, layout, channel, y) + channel % layout.samplesPerPixel(),
            layout.samplesPerPixel()};
}

/// The exact median of a buffer, reordering it in place.
///
/// For an even count it is the mean of the two middle values, found as the middle element after a
/// partial sort and the largest element below it.
///
/// @tparam ValueT the element type.
/// @param values the buffer, reordered on return.
/// @param count how many elements; at least one.
/// @return the median, in double.
template <typename ValueT> [[nodiscard]] inline double exactMedian(ValueT* values, std::uint64_t count) noexcept
{
    ValueT* middle = values + count / 2u;
    std::nth_element(values, middle, values + count);
    double result = static_cast<double>(*middle);
    if ((count & 1u) == 0u)
    {
        result = (static_cast<double>(*std::max_element(values, middle)) + result) * 0.5;
    }
    return result;
}

/// One block of working memory taken from an execution's engine context and returned when the owner
/// goes out of scope, on every exit path.
///
/// The memory is charged to the context's budget and counters like every other engine allocation,
/// which is how a narrowband operation's rank or scratch memory is refused by a budget before any
/// output is written. Neither copyable nor movable; one owner per operation call.
class ContextBlock
{
public:
    /// An owner of nothing.
    ContextBlock() noexcept = default;

    /// Not copyable: two owners would return one block twice.
    ContextBlock(const ContextBlock&) = delete;

    /// Not assignable, for the same reason.
    ContextBlock& operator=(const ContextBlock&) = delete;

    /// Returns the block, if one was taken, to the resource it came from.
    ~ContextBlock()
    {
        if (m_pointer != nullptr)
        {
            m_memory->deallocate(m_pointer, m_bytes, alignof(double));
        }
    }

    /// Takes the block, aligned for `double`, from the execution's memory resource.
    ///
    /// A second successful take is refused with `ErrorCode::InvalidState`, preserving the first block.
    ///
    /// @param execution the operation whose engine context pays for the block.
    /// @param bytes how many bytes.
    /// @param operation the public operation's name for a failure.
    /// @return success; `ErrorCode::NotInitialized` when the execution has no memory resource; or the
    ///         resource's refusal unchanged, such as `ErrorCode::MemoryBudgetExceeded`.
    [[nodiscard]] Status take(const ExecutionContext& execution, std::uint64_t bytes, const char* operation)
    {
        if (m_memory != nullptr)
        {
            return failure(operation, ErrorCode::InvalidState, "this owner already holds a block");
        }
        MemoryResource* memory = execution.memoryResource();
        if (memory == nullptr)
        {
            return failure(operation, ErrorCode::NotInitialized, "the execution has no memory resource");
        }
        Result<void*> allocation = memory->allocate(bytes, alignof(double));
        if (!allocation.hasValue())
        {
            return allocation.status();
        }
        m_memory = memory;
        m_pointer = *allocation;
        m_bytes = bytes;
        return Status::success();
    }

    /// The block as doubles.
    ///
    /// @return its first byte, or null before a successful `take`.
    [[nodiscard]] double* doubles() const noexcept
    {
        return static_cast<double*>(m_pointer);
    }

    /// The block as another element type, whose alignment must not exceed `double`'s.
    ///
    /// @tparam ValueT the element type.
    /// @return its first byte, or null before a successful `take`.
    template <typename ValueT> [[nodiscard]] ValueT* values() const noexcept
    {
        return static_cast<ValueT*>(m_pointer);
    }

private:
    /// Where the block came from and goes back to.
    MemoryResource* m_memory = nullptr;

    /// The block, or null.
    void* m_pointer = nullptr;

    /// Its size, which the resource needs back.
    std::uint64_t m_bytes = 0u;
};

} // namespace an::narrowband_detail
