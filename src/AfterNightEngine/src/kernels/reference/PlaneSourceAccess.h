// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - validation, typed reading, partitioning and scratch shared by the
// algorithm executors that read a resident image component.
//
// Many algorithm families read an image as it is stored rather than after a repack, and each of
// them has to answer the same four questions before it touches a sample: is this execution allowed
// to run, does this pointer and layout describe the representation it claims, which samples does
// the validity rule select, and how is the work divided so that the answer does not depend on the
// machine. Writing those four answers once is what stops the families disagreeing about them.
//
// It sits in the reference-kernel layer rather than beside the executors because the algorithms
// layer keeps one directory per domain and a domain may not reach into another's, so what two
// domains share lives one layer down. Nothing here executes a kernel; the run shapes an optimized
// backend is reached through are in `kernels/reference/AlgorithmKernels.h`.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <cstdint>
#include <limits>

/// Validation, typed reading, deterministic partitioning and context-owned scratch shared by the
/// algorithm families that read a resident image component.
namespace an::plane_detail
{

/// Where one channel's first sample of one row sits, in bytes from the buffer's first.
///
/// It resolves a channel against planar, interleaved and mixed storage in one expression, which is
/// what lets a family read an image as it is resident rather than after a repack.
///
/// @param layout the geometry the channel belongs to.
/// @param channelIndex which channel, below the layout's channel count.
/// @param y the row.
/// @return the byte offset.
[[nodiscard]] inline std::uint64_t channelRowByteOffset(const ImageLayout& layout, std::uint32_t channelIndex,
                                                        std::uint64_t y) noexcept
{
    const std::uint32_t plane = channelIndex / layout.samplesPerPixel();
    const std::uint32_t within = channelIndex % layout.samplesPerPixel();
    return static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
           static_cast<std::uint64_t>(within) * static_cast<std::uint64_t>(layout.elementSizeBytes());
}

/// Where a selected source window sits inside a containing mask, and what else narrows it.
///
/// A source may be the whole image or a rectangle of it addressed through the original strides.
/// When it is a rectangle, the packed mask still describes the complete image, so the two
/// coordinate systems differ by the rectangle's origin and the mask is no longer required to have
/// exactly the source extent. The value interval is the other half of a clipped selection: it is
/// applied after the validity rule rather than instead of it, so a mask and a clip window compose.
///
/// The default is the whole source, an exactly covering mask, and no interval, which is what every
/// family reading a complete plane already meant.
struct PlaneWindow
{
    /// First mask column corresponding to source column zero.
    std::uint32_t maskOriginX = 0u;

    /// First mask row corresponding to source row zero.
    std::uint32_t maskOriginY = 0u;

    /// Whether the mask must have exactly the source extent rather than merely contain the window.
    bool requireExactMaskExtent = true;

    /// Whether a closed value interval narrows the selection beyond its validity rule.
    bool hasValueWindow = false;

    /// The interval's inclusive lower endpoint, read only when one is present.
    double lowestValue = 0.0;

    /// The interval's inclusive upper endpoint, read only when one is present.
    double highestValue = 0.0;

    /// Whether every admitted sample is read as held to a closed interval.
    ///
    /// A hold changes no sample's admission: the validity rule and the value window decide on the stored value, and
    /// the held value is what an admitted sample is then ordered or gathered as. It is how a reader measures samples
    /// as a display holds them without a copy of them held first.
    bool hasHold = false;

    /// The hold's inclusive lower endpoint, read only when one is present.
    double holdLowest = 0.0;

    /// The hold's inclusive upper endpoint, read only when one is present.
    double holdHighest = 0.0;
};

/// Whether an execution may run and a resident component describes what it claims to.
///
/// Checks the execution and its cancellation state, resolves the backend so a forced one that this
/// build or host cannot serve fails before any work, and then checks the pointer, the agreement
/// between the sample type and the layout's element width, the selected plane and component, the
/// pointer's alignment for its representation, and the packed mask's coverage of the extent.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the resident component the caller intends to read.
/// @param validity the value rule and optional packed mask.
/// @param operation the name carried by any failure.
/// @return success, or the first reason the call cannot proceed, in the order checked:
///         `ErrorCode::NotInitialized` for an empty execution; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` for a stopped one; `ErrorCode::UnsupportedBackend` for a
///         forced backend this build has no table for; `ErrorCode::InvalidArgument` for a null
///         pointer or layout, an unknown sample type, or a misaligned pointer;
///         `ErrorCode::IncompatibleArguments` when the element width, plane or component disagrees
///         with the layout; `ErrorCode::InvalidArgument` for a mask supplied to a rule that reads
///         none or missing from the explicit-mask rule; and `ErrorCode::IncompatibleArguments` for
///         a mask that does not have the source extent or the selected bit-plane.
[[nodiscard]] Status checkPlaneSource(const ExecutionContext& execution, const KernelPlaneSource& source,
                                      const KernelValidityView& validity, const char* operation);

/// Whether an execution may run and a windowed component describes what it claims to.
///
/// The same checks, with the mask required to contain the window rather than to match the source
/// exactly when the window says so.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the resident component the caller intends to read.
/// @param validity the value rule and optional packed mask.
/// @param window where that component sits inside the mask.
/// @param operation the name carried by any failure.
/// @return success, or the same failures as the overload without a window, with
///         `ErrorCode::IncompatibleArguments` for a mask that does not contain the window when the
///         window does not require the exact extent.
[[nodiscard]] Status checkPlaneSource(const ExecutionContext& execution, const KernelPlaneSource& source,
                                      const KernelValidityView& validity, const PlaneWindow& window,
                                      const char* operation);

/// Whether one packed mask bit selects a pixel.
///
/// @param validity the rule and its optional mask. A view with no mask accepts every pixel.
/// @param x the column.
/// @param y the row.
/// @return true when no mask is present or the pixel's bit is set.
[[nodiscard]] bool maskAllows(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept;

/// Whether one packed mask bit selects a pixel of a windowed source.
///
/// @param validity the rule and its optional mask. A view with no mask accepts every pixel.
/// @param window where the source sits inside that mask.
/// @param x the column, in source coordinates.
/// @param y the row, in source coordinates.
/// @return true when no mask is present or the corresponding mask bit is set.
[[nodiscard]] bool maskAllows(const KernelValidityView& validity, const PlaneWindow& window, std::uint32_t x,
                              std::uint32_t y) noexcept;

/// The inlinable form used by typed plane readers in their inner loops.
///
/// The same answer as the windowed `maskAllows`, visible to the compiler at the call site. It reads
/// the mask without a bounds test, so the window and the pixel must already have passed
/// `checkPlaneSource`.
[[nodiscard]] inline bool maskAllowsInline(const KernelValidityView& validity, const PlaneWindow& window,
                                           std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
    const std::uint32_t maskX = window.maskOriginX + x;
    const std::uint32_t maskY = window.maskOriginY + y;
    const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(maskY) * mask.rowStrideBytes() + maskX / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (maskX % 8u))) != 0u;
}

/// One rectangle of a source, described as a source of its own without moving a sample.
struct PlaneRectangle
{
    /// The rectangle-sized geometry, which keeps the source row and plane strides.
    ImageLayout layout;

    /// First byte of the rectangle inside the source storage.
    const void* samples = nullptr;

    /// Where the rectangle sits inside a complete-source mask, and what narrows it.
    PlaneWindow window{};
};

/// Describes one rectangle and optional value interval of an already-checked source.
///
/// Two families read part of a plane, and re-deriving the sub-layout in each of them is how the
/// two would come to disagree about which sample a rectangle starts at. The rectangle must be
/// non-empty and contained; an empty one is a selection the caller reports rather than a geometry
/// this can describe.
///
/// @param execution the operation, whose limits the rectangle geometry is checked against.
/// @param source the complete component, already accepted by `checkPlaneSource`.
/// @param selection the contained rectangle and optional value interval.
/// @param operation the name carried by any failure.
/// @return the described rectangle, or `ErrorCode::InvalidArgument` when it lies outside the
///         source, cannot be represented within the context limits, is incompatible with the
///         source strides, or the interval is not a finite ordered closed one.
[[nodiscard]] Result<PlaneRectangle> describePlaneRectangle(const ExecutionContext& execution,
                                                            const KernelPlaneSource& source,
                                                            const KernelPlaneSelection& selection,
                                                            const char* operation);

/// The half-open range of items one chunk of a fixed partition covers.
///
/// The partition is a function of the item and chunk counts alone, so the same input divides the
/// same way at every thread count. Chunks differ in length by at most one item, and the longer ones
/// come first.
///
/// @param index which chunk, below `chunkCount`.
/// @param itemCount how many items the partition covers.
/// @param chunkCount how many chunks it is divided into. Must not be zero.
/// @return that chunk's range.
[[nodiscard]] TaskRange deterministicRange(std::uint64_t index, std::uint64_t itemCount,
                                           std::uint64_t chunkCount) noexcept;

/// One context-accounted allocation released when the operation leaves its scope.
///
/// The engine context's memory resource is where an operation's working memory comes from, so it is
/// counted against the configured budget and a refusal is a returned failure rather than a throw.
/// This owns one such block and gives it back however the operation ends.
class ScopedContextMemory
{
public:
    /// An owner of nothing, which releases nothing.
    ScopedContextMemory() noexcept = default;

    /// Takes ownership of another's block, leaving it owning nothing.
    ///
    /// @param other the owner to take from.
    ScopedContextMemory(ScopedContextMemory&& other) noexcept;

    /// Releases anything held and takes ownership of another's block.
    ///
    /// @param other the owner to take from.
    /// @return this owner.
    ScopedContextMemory& operator=(ScopedContextMemory&& other) noexcept;

    ScopedContextMemory(const ScopedContextMemory&) = delete;
    ScopedContextMemory& operator=(const ScopedContextMemory&) = delete;

    /// Returns the block to the resource it came from.
    ~ScopedContextMemory();

    /// Takes one aligned block from the execution's engine context.
    ///
    /// @param execution the operation whose context owns the memory.
    /// @param bytes how many bytes. Zero yields an owner of nothing rather than a failure.
    /// @param alignment the required alignment, a power of two.
    /// @param operation the name carried by any failure.
    /// @return the owner; `ErrorCode::NotInitialized` when the execution has no memory resource; or
    ///         the resource's own refusal unchanged - `ErrorCode::MemoryBudgetExceeded` carrying the
    ///         requested, available and limit byte counts, `ErrorCode::AllocationFailed`, or
    ///         `ErrorCode::InvalidArgument` for an alignment that is not a power of two.
    [[nodiscard]] static Result<ScopedContextMemory> take(const ExecutionContext& execution, std::uint64_t bytes,
                                                          std::uint64_t alignment, const char* operation);

    /// The first byte of the block, or null when nothing is held.
    ///
    /// @return the pointer, which stays valid until this owner is destroyed or moved from.
    [[nodiscard]] void* data() const noexcept
    {
        return m_pointer;
    }

    /// How many bytes are held.
    ///
    /// @return the byte count, which is zero exactly when nothing is held.
    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return m_bytes;
    }

private:
    /// Builds an owner of one block.
    ///
    /// @param resource where the block came from and goes back to.
    /// @param pointer the block.
    /// @param bytes its size.
    /// @param alignment the alignment it was taken with.
    ScopedContextMemory(MemoryResource& resource, void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept;

    /// Where the block goes back to, or null when nothing is held.
    MemoryResource* m_resource = nullptr;

    /// The block, or null when nothing is held.
    void* m_pointer = nullptr;

    /// How many bytes the block holds.
    std::uint64_t m_bytes = 0u;

    /// The alignment the block was taken with, which the resource needs to release it.
    std::uint64_t m_alignment = 0u;
};

/// A sequential walk through one packed-mask row.
///
/// Resolving the row and first bit once avoids repeating a divide and remainder for every sample
/// in each statistics pass. A cursor with no row admits everything, which keeps unmasked and
/// masked row scans on the same straight-line call site.
class PlaneMaskCursor
{
public:
    /// Builds an all-admitting cursor.
    PlaneMaskCursor() noexcept = default;

    /// Starts at one bit of a packed row.
    ///
    /// @param row first byte of the complete mask row.
    /// @param x first bit to inspect.
    PlaneMaskCursor(const std::uint8_t* row, std::uint32_t x) noexcept
        : m_byte(row == nullptr ? nullptr : row + x / 8u), m_bit(static_cast<std::uint8_t>(1u << (x % 8u)))
    {
    }

    /// Answers the current bit and advances to the next one.
    ///
    /// @return true when no mask participates or the current mask bit is set.
    [[nodiscard]] bool allowsAndAdvance() noexcept
    {
        if (m_byte == nullptr)
        {
            return true;
        }
        const bool allowed = (*m_byte & m_bit) != 0u;
        m_bit = static_cast<std::uint8_t>(m_bit << 1u);
        if (m_bit == 0u)
        {
            m_bit = 1u;
            ++m_byte;
        }
        return allowed;
    }

    /// Visits the selected offsets in the next run and advances past all of it.
    ///
    /// Complete selected bytes take one mask branch for eight samples, while a partial byte keeps
    /// the exact bitwise path. The callback returns false to stop the walk early.
    ///
    /// @tparam CallableT a callable accepting a zero-based `std::uint64_t` offset and returning bool.
    /// @param count how many consecutive bits belong to the run.
    /// @param callable invoked for every selected offset.
    /// @return false when the callback stopped the walk; true after the whole run.
    template <typename CallableT> [[nodiscard]] bool forEachAllowed(std::uint64_t count, CallableT&& callable) noexcept
    {
        if (m_byte == nullptr)
        {
            for (std::uint64_t offset = 0u; offset < count; ++offset)
            {
                if (!callable(offset))
                {
                    return false;
                }
            }
            return true;
        }

        std::uint64_t offset = 0u;
        while (offset < count && m_bit != 1u)
        {
            if (allowsAndAdvance() && !callable(offset))
            {
                return false;
            }
            ++offset;
        }
        while (count - offset >= 8u)
        {
            const std::uint8_t selected = *m_byte;
            ++m_byte;
            if (selected == 0xffu)
            {
                for (std::uint64_t bit = 0u; bit < 8u; ++bit)
                {
                    if (!callable(offset + bit))
                    {
                        return false;
                    }
                }
            }
            else if (selected != 0u)
            {
                for (std::uint64_t bit = 0u; bit < 8u; ++bit)
                {
                    if ((selected & static_cast<std::uint8_t>(1u << bit)) != 0u && !callable(offset + bit))
                    {
                        return false;
                    }
                }
            }
            offset += 8u;
        }
        while (offset < count)
        {
            if (allowsAndAdvance() && !callable(offset))
            {
                return false;
            }
            ++offset;
        }
        return true;
    }

private:
    /// Byte containing the current bit, or null when no mask participates.
    const std::uint8_t* m_byte = nullptr;

    /// The current one-hot bit.
    std::uint8_t m_bit = 1u;
};

/// Reads one resident representation out of a checked component, widening every sample once.
///
/// The widening is to `double`, which represents all six integer representations and Float32
/// exactly, so an algorithm defined on one processing domain reads eight storage types without
/// eight versions of itself. It borrows what it is built from and must not outlive it.
///
/// @tparam SampleT the resident C++ type the component holds.
template <ResidentSample SampleT> class PlaneSampleReader
{
public:
    /// Borrows a checked complete component and the rule that selects from it.
    ///
    /// @param source the component, already accepted by `checkPlaneSource`.
    /// @param validity the value rule and optional packed mask.
    PlaneSampleReader(const KernelPlaneSource& source, const KernelValidityView& validity) noexcept
        : m_source(source), m_validity(validity), m_window(kWholeSource),
          m_valueRuleAcceptsAll(validity.descriptor.rule() == ValidityRule::AllValid ||
                                validity.descriptor.rule() == ValidityRule::ExplicitMask)
    {
    }

    /// Borrows a checked windowed component, the rule that selects from it, and that window.
    ///
    /// @param source the component, already accepted by `checkPlaneSource`.
    /// @param validity the value rule and optional packed mask.
    /// @param window where the component sits inside the mask, and the value interval narrowing it.
    ///        Borrowed for this reader's life.
    PlaneSampleReader(const KernelPlaneSource& source, const KernelValidityView& validity,
                      const PlaneWindow& window) noexcept
        : m_source(source), m_validity(validity), m_window(window),
          m_valueRuleAcceptsAll(validity.descriptor.rule() == ValidityRule::AllValid ||
                                validity.descriptor.rule() == ValidityRule::ExplicitMask)
    {
    }

    /// How many pixels one row holds.
    ///
    /// @return the width.
    [[nodiscard]] std::uint32_t width() const noexcept
    {
        return m_source.layout->extent().width();
    }

    /// How many rows the component holds.
    ///
    /// @return the height.
    [[nodiscard]] std::uint32_t height() const noexcept
    {
        return m_source.layout->extent().height();
    }

    /// How many samples separate one pixel's selected component from the next along a row.
    ///
    /// @return one for planar storage, and the pixel's sample count for interleaved storage.
    [[nodiscard]] std::uint32_t columnStride() const noexcept
    {
        return m_source.layout->samplesPerPixel();
    }

    /// The selected component of the first pixel of one row.
    ///
    /// @param y the row, below `height()`.
    /// @return the pointer, from which the row is walked in `columnStride()` steps.
    [[nodiscard]] const SampleT* rowStart(std::uint32_t y) const noexcept
    {
        const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(m_source.samples);
        return reinterpret_cast<const SampleT*>(
            bytes + *m_source.layout->sampleByteOffset(m_source.plane, y, 0u, m_source.sampleInPixel));
    }

    /// First packed-mask byte of one source row, or null when no mask participates.
    ///
    /// Hoisting the plane and row strides out of a scan keeps the per-sample mask test to one byte
    /// load and one bit operation.
    [[nodiscard]] const std::uint8_t* maskRowStart(std::uint32_t y) const noexcept
    {
        if (m_validity.mask == nullptr)
        {
            return nullptr;
        }
        const ValidityMaskView<const std::uint8_t>& mask = *m_validity.mask;
        const std::uint64_t offset = static_cast<std::uint64_t>(m_validity.maskPlane) * mask.planeStrideBytes() +
                                     static_cast<std::uint64_t>(m_window.maskOriginY + y) * mask.rowStrideBytes();
        return mask.data() + offset;
    }

    /// A cursor beginning at one source column of a mask row.
    ///
    /// @param y the source row.
    /// @param x the first source column the scan will inspect.
    /// @return a cursor over the corresponding mask bits, or an all-admitting cursor when unmasked.
    [[nodiscard]] PlaneMaskCursor maskCursor(std::uint32_t y, std::uint32_t x) const noexcept
    {
        return PlaneMaskCursor(maskRowStart(y), m_window.maskOriginX + x);
    }

    /// Whether the selected component is one gapless span and every stored sample participates.
    ///
    /// This is the hot-path proof used by scanners that can then avoid repeating validity, mask,
    /// interval and row-stride decisions for every sample.
    ///
    /// @return true when `contiguousSamples()` names exactly `width() * height()` selected samples.
    [[nodiscard]] bool contiguousAllValid() const noexcept
    {
        return contiguousUnmaskedValues() && !m_window.hasValueWindow;
    }

    /// Whether a gapless, all-valid component is selected only by its optional value interval.
    /// @return true when a scanner may walk one span and apply `valueWindowAllows` alone.
    [[nodiscard]] bool contiguousUnmaskedValues() const noexcept
    {
        return m_source.layout->samplesPerPixel() == 1u &&
               m_source.layout->rowStrideBytes() ==
                   static_cast<std::uint64_t>(width()) * static_cast<std::uint64_t>(sizeof(SampleT)) &&
               unmaskedValues();
    }

    /// Whether every stored sample is selected but for the optional value interval, whatever the strides.
    ///
    /// A scanner may then read any sample by its position and test the interval alone.
    ///
    /// @return true when neither a value rule nor a mask can exclude a sample.
    [[nodiscard]] bool unmaskedValues() const noexcept
    {
        return m_validity.descriptor.rule() == ValidityRule::AllValid && m_validity.mask == nullptr;
    }

    /// Whether a packed mask selects among the pixels, so that a scan reads a bit per pixel.
    ///
    /// @return true when a mask participates.
    [[nodiscard]] bool hasMask() const noexcept
    {
        return m_validity.mask != nullptr;
    }

    /// Whether an optional value interval narrows the selection.
    ///
    /// @return true when `valueWindowAllows` can refuse a sample.
    [[nodiscard]] bool hasValueWindow() const noexcept
    {
        return m_window.hasValueWindow;
    }

    /// The value interval's inclusive first value, or minus infinity when no interval narrows the selection.
    ///
    /// @return the first value the interval admits.
    [[nodiscard]] double valueWindowLowest() const noexcept
    {
        return m_window.hasValueWindow ? m_window.lowestValue : -std::numeric_limits<double>::infinity();
    }

    /// The value interval's inclusive last value, or infinity when no interval narrows the selection.
    ///
    /// @return the last value the interval admits.
    [[nodiscard]] double valueWindowHighest() const noexcept
    {
        return m_window.hasValueWindow ? m_window.highestValue : std::numeric_limits<double>::infinity();
    }

    /// Tests only the optional value interval after the other selection rules were proved.
    /// @param value the exactly widened sample.
    /// @return whether the closed interval admits the sample.
    [[nodiscard]] bool valueWindowAllows(double value) const noexcept
    {
        return !m_window.hasValueWindow || (value >= m_window.lowestValue && value <= m_window.highestValue);
    }

    /// Whether every admitted sample is read as held to a closed interval.
    ///
    /// @return true when `held` can change a value.
    [[nodiscard]] bool hasHold() const noexcept
    {
        return m_window.hasHold;
    }

    /// The hold's inclusive first value, or minus infinity when no hold is read through.
    ///
    /// @return the smallest value an admitted sample is read as.
    [[nodiscard]] double holdLowest() const noexcept
    {
        return m_window.hasHold ? m_window.holdLowest : -std::numeric_limits<double>::infinity();
    }

    /// The hold's inclusive last value, or infinity when no hold is read through.
    ///
    /// @return the largest value an admitted sample is read as.
    [[nodiscard]] double holdHighest() const noexcept
    {
        return m_window.hasHold ? m_window.holdHighest : std::numeric_limits<double>::infinity();
    }

    /// An admitted sample's widened value as the hold reads it.
    ///
    /// The lower end first and then the upper, each through the standard minimum and maximum, which is the order and
    /// the arithmetic a clamp applies, so a value at an endpoint and a signed zero read as the clamp reads them.
    ///
    /// @param value the exactly widened sample, already admitted.
    /// @return the value held to the interval, or the value itself when no hold is read through.
    [[nodiscard]] double held(double value) const noexcept
    {
        return m_window.hasHold ? (std::min)((std::max)(value, m_window.holdLowest), m_window.holdHighest) : value;
    }

    /// First sample of the component when `contiguousUnmaskedValues()` is true.
    ///
    /// @return the component pointer; callers must prove the contiguous fast-path predicate first.
    [[nodiscard]] const SampleT* contiguousSamples() const noexcept
    {
        return rowStart(0u);
    }

    /// Whether one sample participates, and its widened value when it does.
    ///
    /// @param value the stored sample.
    /// @param x its column.
    /// @param y its row.
    /// @param widened where the widened value goes when the sample participates.
    /// @return true when the value rule and the mask both accept it.
    [[nodiscard]] bool selected(SampleT value, std::uint32_t x, std::uint32_t y, double& widened) const noexcept
    {
        if ((!m_valueRuleAcceptsAll && !sampleIsValidByValue(m_validity.descriptor, value)) ||
            !maskAllowsInline(m_validity, m_window, x, y))
        {
            return false;
        }
        widened = static_cast<double>(value);
        // The interval narrows the rule rather than replacing it, and it is compared on the widened
        // value so that all eight representations decide membership in one domain. An admitted value is then
        // read as the hold reads it.
        if (m_window.hasValueWindow && !(widened >= m_window.lowestValue && widened <= m_window.highestValue))
        {
            return false;
        }
        widened = held(widened);
        return true;
    }

    /// Whether the non-mask rules select one sample whose mask bit was already accepted, and its widened value as
    /// the hold reads it when they do.
    [[nodiscard]] bool selectedAfterMask(SampleT value, double& widened) const noexcept
    {
        if (!m_valueRuleAcceptsAll && !sampleIsValidByValue(m_validity.descriptor, value))
        {
            return false;
        }
        widened = static_cast<double>(value);
        if (m_window.hasValueWindow && !(widened >= m_window.lowestValue && widened <= m_window.highestValue))
        {
            return false;
        }
        widened = held(widened);
        return true;
    }

private:
    /// The window a reader built from a complete plane borrows, so that both constructors can hold
    /// a reference rather than one of them holding a copy nothing else can see.
    static constexpr PlaneWindow kWholeSource{};

    /// The component being read, borrowed for this reader's life.
    const KernelPlaneSource& m_source;

    /// The rule selecting from it, borrowed for this reader's life.
    const KernelValidityView& m_validity;

    /// Where it sits inside the mask, and what narrows it, borrowed for this reader's life.
    const PlaneWindow& m_window;

    /// Whether the value rule itself admits every representation value, leaving only the mask and
    /// value-interval tests.
    bool m_valueRuleAcceptsAll = false;
};

} // namespace an::plane_detail
