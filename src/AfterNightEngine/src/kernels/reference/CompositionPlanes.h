// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - planar working images for the compositions that read images of any representation.
//
// An image combination reads two or three images that may be stored in any of the eight representations,
// planar or interleaved, with a validity rule each, and writes one image in a representation the caller states.
// What it computes in between is a chain of the engine's own operations over Float32 working planes - Float64
// when the result is Float64 - so every one of them reads the same thing: one tightly packed plane per channel,
// every sample a fraction of full scale, every sample finite. This header is the one place that turns a source
// into that shape and turns the result back.
//
// It sits in the reference-kernel layer because the compositions live in three algorithm domains - colour,
// pointwise and stars - and a domain may not include another's private headers. Nothing here executes a kernel.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace an
{

class ExecutionContext;
class ImageDescriptor;
class MemoryResource;

/// Working planes, source reading and destination writing shared by the image compositions.
namespace composition_detail
{

/// One block of working memory from an execution's engine context, returned when the block goes.
///
/// Moving is refused as well as copying, so the block a pointer was taken from is the block that frees it.
class WorkingBlock
{
public:
    /// An empty block.
    WorkingBlock() noexcept = default;

    /// Not copyable.
    WorkingBlock(const WorkingBlock&) = delete;

    /// Not copyable.
    WorkingBlock& operator=(const WorkingBlock&) = delete;

    /// Returns the memory, if any was taken.
    ~WorkingBlock();

    /// Takes `bytes` aligned for the widest vector from the execution's memory resource.
    ///
    /// @param execution whose engine context's resource the memory comes from.
    /// @param bytes how many bytes; zero takes nothing and succeeds.
    /// @param operation the engine operation a failure names.
    /// @return success; `ErrorCode::NotInitialized` for an execution without a resource; or the resource's
    ///         refusal, which carries what was asked for, what was available and the limit.
    [[nodiscard]] Status take(const ExecutionContext& execution, std::uint64_t bytes, const char* operation);

    /// The first byte taken, or null before a successful `take`.
    [[nodiscard]] std::uint8_t* bytes() const noexcept
    {
        return static_cast<std::uint8_t*>(m_pointer);
    }

private:
    /// The resource the bytes came from.
    MemoryResource* m_memory = nullptr;

    /// The first byte, or null.
    void* m_pointer = nullptr;

    /// How many bytes were taken.
    std::uint64_t m_bytes = 0u;
};

/// One image a composition reads, as the operation's own source record names it.
///
/// Every member is borrowed for the call.
struct CompositionSource
{
    /// What the samples are.
    const ImageDescriptor* descriptor = nullptr;

    /// The first byte of the samples.
    const void* samples = nullptr;

    /// Where the samples sit.
    const ImageLayout* layout = nullptr;

    /// Which samples mean something. A mask with one plane per channel of a multi-channel source gives each
    /// channel its own plane; any other mask gives every channel the view's `maskPlane`.
    KernelValidityView validity{};
};

/// Whether a source's members describe one another and its mask covers it.
///
/// @param source the source to check.
/// @param operation the engine operation a failure names.
/// @param parameter the argument a failure names.
/// @return success; `ErrorCode::InvalidArgument` for a null member, a layout or pointer that does not describe
///         the descriptor, or a validity mask that is missing or unwanted; or `ErrorCode::IncompatibleArguments`
///         for a validity mask that does not cover the source.
[[nodiscard]] Status checkCompositionSource(const CompositionSource& source, const char* operation,
                                            const char* parameter);

/// Whether a source has a rule that can make a sample invalid: a value rule other than all-valid, or a mask.
///
/// @param source a checked source.
/// @return true when some pixel of it may be invalid.
[[nodiscard]] bool compositionSourceRuled(const CompositionSource& source) noexcept;

/// Whether a destination is a buffer of the stated representation and extent.
///
/// @param sampleType the destination's representation.
/// @param destination the first byte written.
/// @param layout where the destination's samples sit.
/// @param extent the width, height and channel count the composition writes.
/// @param operation the engine operation a failure names.
/// @return success; `ErrorCode::InvalidArgument` for a null pointer, a representation outside its enumeration,
///         a misaligned pointer or an element size that is not the representation's; or
///         `ErrorCode::IncompatibleArguments` for another extent.
[[nodiscard]] Status checkCompositionDestination(SampleType sampleType, const void* destination,
                                                 const ImageLayout& layout, const ImageExtent& extent,
                                                 const char* operation);

/// Whether a source's samples or validity mask share memory with a destination.
///
/// @param source a checked source.
/// @param destination the destination's first byte.
/// @param destinationBytes how many bytes the destination spans.
/// @param operation the engine operation a failure names.
/// @param parameter the argument a failure names.
/// @return success; `ErrorCode::IncompatibleArguments` when the samples overlap the destination; or
///         `ErrorCode::InvalidArgument` when the validity mask does.
[[nodiscard]] Status checkCompositionSeparate(const CompositionSource& source, const void* destination,
                                              std::uint64_t destinationBytes, const char* operation,
                                              const char* parameter);

/// A planar Float32 or Float64 working image: one tightly packed plane per channel, one block of memory.
struct WorkingPlanes
{
    /// Float32 or Float64.
    SampleType sampleType = SampleType::Float32;

    /// The first byte of the first plane.
    std::uint8_t* samples = nullptr;

    /// Every plane.
    std::optional<ImageLayout> layout;

    /// The channel count.
    [[nodiscard]] std::uint32_t channels() const noexcept
    {
        return layout->extent().channelCount();
    }

    /// The first byte of one plane.
    [[nodiscard]] std::uint8_t* plane(std::uint32_t channel) const noexcept
    {
        return samples + static_cast<std::uint64_t>(channel) * layout->planeStrideBytes();
    }

    /// One row of one plane, as the working type's samples.
    template <typename SampleT> [[nodiscard]] SampleT* row(std::uint32_t channel, std::uint32_t y) const noexcept
    {
        return reinterpret_cast<SampleT*>(plane(channel) + static_cast<std::uint64_t>(y) * layout->rowStrideBytes());
    }
};

/// A layout of `planes` planes over a working image's strides, starting at its first plane.
///
/// @param working the working image.
/// @param planes how many planes, from one through the working image's channel count.
/// @return the layout, or nothing when the planes cannot be described.
[[nodiscard]] std::optional<ImageLayout> workingPlaneLayout(const WorkingPlanes& working, std::uint32_t planes);

/// Takes the memory for a working image and describes it.
///
/// @param execution whose engine context's resource and limits are used.
/// @param block the block the memory is taken into; it must be empty and outlive the planes.
/// @param width the width.
/// @param height the height.
/// @param channels the channel count.
/// @param sampleType Float32 or Float64.
/// @param operation the engine operation a failure names.
/// @return the planes; `ErrorCode::InvalidArgument` for an extent or layout the limits refuse; or the
///         resource's refusal.
[[nodiscard]] Result<WorkingPlanes> takeWorkingPlanes(const ExecutionContext& execution, WorkingBlock& block,
                                                      std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                                      SampleType sampleType, const char* operation);

/// Reads every sample of a source into working planes as a fraction of full scale.
///
/// An integer sample is its code over its representation's largest, by the engine's conversion - a signed one a
/// signed fraction - and a floating one is as it is. A sample of a pixel the source's validity rejects in any of
/// its channels is written as zero and the pixel's flag cleared, so every working sample is finite. Runs on the
/// operation's threads.
///
/// @param execution the operation policy and engine-context lease.
/// @param source a checked source of the working image's width and height.
/// @param working the destination planes.
/// @param firstPlane the working plane the source's first channel goes to; the source's channels must fit.
/// @param valid one flag per pixel, row-major, which a rejected pixel clears and nothing sets; null when the
///        source has no rule.
/// @param operation the engine operation a failure names.
/// @return success; `ErrorCode::InvalidData` for a sample that is not finite in a pixel the validity admits,
///         naming the sample; or the execution's cancellation.
[[nodiscard]] Status readWorking(const ExecutionContext& execution, const CompositionSource& source,
                                 const WorkingPlanes& working, std::uint32_t firstPlane, std::uint8_t* valid,
                                 const char* operation);

/// Copies plane `from` of a working image over plane `to`.
///
/// @param working the working image.
/// @param from the plane read.
/// @param to the plane written.
void copyWorkingPlane(const WorkingPlanes& working, std::uint32_t from, std::uint32_t to) noexcept;

/// Copies plane `from` of one working image over plane `to` of another of the same type and extent.
///
/// @param source the image read.
/// @param from the plane read.
/// @param destination the image written.
/// @param to the plane written.
void copyWorkingPlane(const WorkingPlanes& source, std::uint32_t from, const WorkingPlanes& destination,
                      std::uint32_t to) noexcept;

/// Writes a working image to a destination of any representation.
///
/// Every value is converted once by the engine's conversion: to the nearest code with ties to even, saturating
/// at the representation's range, a fraction of full scale for an integer. Runs on the operation's threads.
///
/// @param execution the operation policy and engine-context lease.
/// @param working the planes read; their channel count is the destination's.
/// @param sampleType the destination's representation.
/// @param destination the first byte written, checked by `checkCompositionDestination`.
/// @param destinationLayout where the destination's samples sit.
/// @param operation the engine operation a failure names.
/// @return success, or the execution's cancellation.
[[nodiscard]] Status writeWorking(const ExecutionContext& execution, const WorkingPlanes& working,
                                  SampleType sampleType, void* destination, const ImageLayout& destinationLayout,
                                  const char* operation);

/// Whether a floating-point sample is an infinity or not a number, read from its exponent so that a run can test
/// every sample without a branch.
///
/// @tparam SampleT `float` or `double`.
/// @param value the sample.
/// @return one when the sample is not finite, zero when it is.
template <typename SampleT> [[nodiscard]] std::uint32_t nonFiniteBit(SampleT value) noexcept
{
    if constexpr (sizeof(SampleT) == sizeof(std::uint32_t))
    {
        return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) == 0x7F800000u ? 1u : 0u;
    }
    else
    {
        return (std::bit_cast<std::uint64_t>(value) & 0x7FF0000000000000ull) == 0x7FF0000000000000ull ? 1u : 0u;
    }
}

/// The first sample of one channel's row in an image of any layout; successive samples are the layout's samples per
/// pixel apart.
///
/// @tparam SampleT the resident representation.
/// @param samples the image's first byte.
/// @param layout where its samples sit.
/// @param channel the channel, below the layout's channel count.
/// @param y the row, below the layout's height.
/// @return the sample's address.
template <typename SampleT>
[[nodiscard]] SampleT* channelRowOf(void* samples, const ImageLayout& layout, std::uint32_t channel,
                                    std::uint32_t y) noexcept
{
    return reinterpret_cast<SampleT*>(static_cast<std::uint8_t*>(samples) +
                                      *layout.rowByteOffset(channel / layout.samplesPerPixel(), y)) +
           channel % layout.samplesPerPixel();
}

/// The first sample of one channel's row in a read-only image of any layout.
///
/// @tparam SampleT the resident representation.
/// @param samples the image's first byte.
/// @param layout where its samples sit.
/// @param channel the channel, below the layout's channel count.
/// @param y the row, below the layout's height.
/// @return the sample's address.
template <typename SampleT>
[[nodiscard]] const SampleT* channelRowOf(const void* samples, const ImageLayout& layout, std::uint32_t channel,
                                          std::uint32_t y) noexcept
{
    return reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(samples) +
                                            *layout.rowByteOffset(channel / layout.samplesPerPixel(), y)) +
           channel % layout.samplesPerPixel();
}

} // namespace composition_detail
} // namespace an
