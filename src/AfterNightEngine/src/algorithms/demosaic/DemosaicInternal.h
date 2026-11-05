// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what the demosaic domain shares between the estimate and the rearrangements.
//
// One mosaic in, several planes out, in whatever representation the caller stores. That shape is
// the same for the estimate, the block split, its inverse and the super-pixel, and so is everything
// around it: deciding whether a plane can be reached where it lies, widening it when it cannot,
// converting the answer back under a stated policy, and refusing an argument before a sample is
// read. Writing it once is what stops four operations disagreeing about which of them is allowed.
//
// It is a header of the demosaic domain rather than of the algorithms layer, because a domain may
// not reach into another domain's private headers. The neighbourhood filters answer the same
// questions in their own directory, which is the cost the rule buys: two copies that cannot drift
// into each other rather than one that every domain is free to change.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/PlaneSourceAccess.h"

#include <cstdint>
#include <utility>

/// The argument checks and plane access the demosaic domain's operations share. The representation
/// dispatch is the published one in `Image/SampleTraits.h`.
namespace an::demosaic_detail
{

/// A Float32 plane a kernel reads or writes, either the caller's storage or working memory.
///
/// A pointer and a stride rather than a view, because that is what a kernel takes: the tiled
/// estimates index one plane by absolute picture coordinates, and a view would be unpacked into
/// exactly these two numbers at every call site.
struct FloatPlane
{
    /// First sample of the plane, or null when the plane holds nothing.
    float* data = nullptr;

    /// Floats between one row and the next.
    std::uint64_t stride = 0u;
};

/// One failure from this domain.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status demosaicFailure(ErrorCode code, const char* operation, const char* message);

/// One failure carrying what was asked for and what was allowed.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @param requested the number the caller supplied.
/// @param limit the number the contract allows.
/// @return the failure, carrying both counts.
[[nodiscard]] Status demosaicLimitFailure(ErrorCode code, const char* operation, const char* message,
                                          std::uint64_t requested, std::uint64_t limit);

/// What one row of a widening or narrowing pass costs.
///
/// @param width how many pixels one row holds.
/// @return the bytes one row touches and roughly what it costs to move them.
[[nodiscard]] TaskCost transferRowCost(std::uint32_t width) noexcept;

/// Where one channel's row starts, whatever arrangement the layout describes.
///
/// @param layout the geometry.
/// @param channelIndex which channel, counted in plane-major order.
/// @param y the row.
/// @return the byte offset from the first byte of the image.
[[nodiscard]] std::uint64_t channelRowByteOffset(const ImageLayout& layout, std::uint32_t channelIndex,
                                                 std::uint64_t y) noexcept;

/// Whether two byte ranges share a byte.
///
/// @param first where one range starts.
/// @param firstBytes how long it is.
/// @param second where the other starts.
/// @param secondBytes how long it is.
/// @return true when the two ranges intersect.
[[nodiscard]] bool rangesOverlap(const void* first, std::uint64_t firstBytes, const void* second,
                                 std::uint64_t secondBytes) noexcept;

/// Whether one channel of an image is a Float32 plane a kernel can reach where it lies.
///
/// Four things have to hold at once: the samples are Float32, one pixel holds one sample of this
/// plane, the row and plane strides are whole numbers of samples, and the first byte is aligned for
/// a float. A layout that fails any of them is read through a working plane instead, which is a copy
/// the result reports rather than one it hides.
///
/// @param sampleType the representation.
/// @param layout where the samples sit.
/// @param samples the first byte, whose alignment is part of the answer.
/// @return true when a channel of this image is addressable as a plane.
[[nodiscard]] bool channelIsAddressablePlane(SampleType sampleType, const ImageLayout& layout,
                                             const void* samples) noexcept;

/// Borrows one channel of an addressable Float32 image as a plane.
///
/// Addressability is the caller's question, answered first by `channelIsAddressablePlane`; this only
/// computes where the channel's plane starts and its stride in floats.
///
/// @param samples where the image samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to reach.
/// @return the plane, whose data is null when the image has no such channel.
[[nodiscard]] FloatPlane borrowFloatPlane(void* samples, const ImageLayout& layout,
                                          std::uint32_t channelIndex) noexcept;

/// One working plane taken from the engine context, released when the operation leaves its scope.
class WorkingPlane
{
public:
    /// A plane holding nothing.
    WorkingPlane() noexcept = default;

    /// Takes one Float32 plane of this size from the execution's engine context.
    ///
    /// The plane is tight - its stride is `width` - aligned to a cache line, and its samples start
    /// uninitialised.
    ///
    /// @param execution the operation whose context owns the memory.
    /// @param width how many pixels one row holds.
    /// @param height how many rows.
    /// @param operation the name carried by any failure.
    /// @return the plane, or a plane holding nothing for a zero size; `ErrorCode::NotInitialized`
    ///         when the execution has no memory resource; or the memory resource's refusal carrying
    ///         requested, available and limit bytes.
    [[nodiscard]] static Result<WorkingPlane> take(const ExecutionContext& execution, std::uint32_t width,
                                                   std::uint32_t height, const char* operation);

    /// Whether this holds working memory.
    ///
    /// @return true when a plane was taken.
    [[nodiscard]] bool owned() const noexcept
    {
        return m_memory.bytes() != 0u;
    }

    /// How many bytes of working memory this holds.
    ///
    /// @return the byte count, zero when nothing was taken.
    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return m_memory.bytes();
    }

    /// The plane itself.
    ///
    /// @return the pointer and stride, valid until this is destroyed or moved from.
    [[nodiscard]] FloatPlane plane() const noexcept
    {
        return m_plane;
    }

private:
    /// Builds a holder over one block.
    ///
    /// @param memory the working memory.
    /// @param plane the plane inside it.
    WorkingPlane(plane_detail::ScopedContextMemory&& memory, FloatPlane plane) noexcept
        : m_memory(std::move(memory)), m_plane(plane)
    {
    }

    /// The working memory, or an owner of nothing.
    plane_detail::ScopedContextMemory m_memory{};

    /// The plane inside it, or a plane holding nothing.
    FloatPlane m_plane{};
};

/// Widens one channel of an image into a Float32 working plane.
///
/// Carries every sample across as the number it is, rounded to Float32 where a 32-bit integer or a
/// Float64 carries more precision; it does not rescale. Runs on the operation's threads and checks
/// cancellation at chunk boundaries.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation the layout describes.
/// @param samples where the image samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to read.
/// @param destination the working plane.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param operation the name carried by any failure.
/// @return success; `ErrorCode::InvalidArgument` for a channel the image does not have or a
///         representation outside the enumeration; or the scheduler's cancellation failure.
[[nodiscard]] Status gatherPlane(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                                 const ImageLayout& layout, std::uint32_t channelIndex, FloatPlane destination,
                                 std::uint32_t width, std::uint32_t height, const char* operation);

/// Writes a Float32 working plane back into one channel of an image.
///
/// Every sample goes through `convertSample` under the stated policy. When the source plane is the
/// destination channel itself, a Float32 sample converts to itself, so the pass counts and applies
/// the policy's refusals without changing a sample.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the working plane holding the answer.
/// @param sampleType the destination representation.
/// @param samples where the destination samples start.
/// @param layout where they sit.
/// @param channelIndex which channel to write.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param conversion how a computed value becomes a stored sample.
/// @param counters incremented by the samples converted, the non-finite values met and the values
///        saturated, including those counted before a refusal.
/// @param operation the name carried by any failure.
/// @return success; `ErrorCode::InvalidData` for a non-finite value under
///         `NonFinitePolicy::Reject`; `ErrorCode::OutOfRange` for a value outside the destination
///         representation under `RangePolicy::Reject`; `ErrorCode::InvalidArgument` for a channel the
///         image does not have or a representation outside the enumeration; or the scheduler's
///         cancellation failure. A refusal leaves the rows already converted written.
[[nodiscard]] Status scatterPlane(const ExecutionContext& execution, FloatPlane source, SampleType sampleType,
                                  void* samples, const ImageLayout& layout, std::uint32_t channelIndex,
                                  std::uint32_t width, std::uint32_t height, const ConversionPolicy& conversion,
                                  ConversionCounters& counters, const char* operation);

/// Checks the relationship between a validity rule and its optional packed mask.
///
/// @param validity the rule the source claims.
/// @param mask the packed source bits, when the rule needs them.
/// @param width the exact source width.
/// @param height the exact source height.
/// @param firstPlaneCount one accepted mask plane count.
/// @param secondPlaneCount another accepted count, or zero when there is only one.
/// @param operation the name carried by a failure.
/// @return success, `ErrorCode::InvalidArgument` when rule and presence contradict one another, or
///         `ErrorCode::IncompatibleArguments` when the mask geometry does not cover this source.
[[nodiscard]] Status checkSelectionMask(const ValidityDescriptor& validity,
                                        const ValidityMaskView<const std::uint8_t>* mask, std::uint32_t width,
                                        std::uint32_t height, std::uint32_t firstPlaneCount,
                                        std::uint32_t secondPlaneCount, const char* operation);

/// Checks one optional produced mask's exact geometry.
///
/// Presence is an operation-specific question checked by the caller; this helper checks only a
/// mask that is present.
///
/// @param mask the produced bits, or null.
/// @param width the exact destination width.
/// @param height the exact destination height.
/// @param firstPlaneCount one accepted mask plane count.
/// @param secondPlaneCount another accepted count, or zero when there is only one.
/// @param operation the name carried by a failure.
/// @return success when absent or matching; otherwise `ErrorCode::IncompatibleArguments`.
[[nodiscard]] Status checkProducedMask(const ValidityMaskView<std::uint8_t>* mask, std::uint32_t width,
                                       std::uint32_t height, std::uint32_t firstPlaneCount,
                                       std::uint32_t secondPlaneCount, const char* operation);

/// Refuses a mask whose bytes would be overwritten while they are still being read.
///
/// Read-only sample and mask buffers may overlap one another. Every writable buffer must be
/// disjoint from every input and from the other writable buffer, because the domain operations do
/// not define an in-place order for samples and packed bits.
///
/// @param source source sample bytes.
/// @param sourceBytes number of source sample bytes.
/// @param destination destination sample bytes.
/// @param destinationBytes number of destination sample bytes.
/// @param sourceMask optional read-only validity bits.
/// @param producedMask optional writable validity bits.
/// @param operation the name carried by a failure.
/// @return success, or `ErrorCode::Aliasing` when a written byte range intersects another range.
[[nodiscard]] Status checkMaskAliasing(const void* source, std::uint64_t sourceBytes, const void* destination,
                                       std::uint64_t destinationBytes,
                                       const ValidityMaskView<const std::uint8_t>* sourceMask,
                                       const ValidityMaskView<std::uint8_t>* producedMask, const char* operation);

/// Whether an execution may run and two images describe pictures this domain can work between.
///
/// The shared precondition, in the order the domain promises: the execution and its cancellation
/// state, then the pointers and the representation, then the layouts against the representation and
/// the expected channel counts, then whether the two buffers overlap.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation both sides claim.
/// @param source where the source samples start.
/// @param sourceLayout where they sit.
/// @param sourceChannels how many channels the source must have.
/// @param destination where the results go.
/// @param destinationLayout where they sit.
/// @param destinationChannels how many channels the destination must have.
/// @param operation the name carried by any failure.
/// @return success, or the first reason the call cannot proceed: `ErrorCode::NotInitialized` for
///         an execution that holds nothing; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`
///         for one already stopped; `ErrorCode::InvalidArgument` for a null pointer, a
///         representation outside the enumeration, a layout whose element size is not the
///         representation's, or a pointer not aligned for it; `ErrorCode::IncompatibleArguments`
///         for a channel count other than the expected one; or `ErrorCode::Aliasing` when the two
///         images' byte spans overlap.
[[nodiscard]] Status checkImagePair(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                    const ImageLayout& sourceLayout, std::uint32_t sourceChannels,
                                    const void* destination, const ImageLayout& destinationLayout,
                                    std::uint32_t destinationChannels, const char* operation);

} // namespace an::demosaic_detail
