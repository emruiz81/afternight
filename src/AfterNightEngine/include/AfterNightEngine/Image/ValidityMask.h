// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A packed validity mask that owns its bits, and the few things done to one.
///
/// `Image/ValidityMaskView.h` is a mask borrowed; this is a mask owned. The memory comes from an
/// engine context's resource, is counted against its budget, and is given back when the mask dies -
/// the same arrangement `Image/ImageStorage.h` makes for samples, and for the same reason: a mask is
/// about 3 MB for a 24-megapixel frame, which is small beside the frame and large beside nothing, and
/// memory the engine hands out and does not count is memory nobody can budget for.
///
/// **A mask is not part of an image.** `Image/Image.h` holds samples, a description and metadata, and
/// the description says whether a mask decides; the mask itself travels beside the image, held by
/// whatever owns both. That is a deliberate separation rather than an omission. A mask is memory, and
/// putting memory inside the object that carries meaning would make every metadata copy a question
/// about allocation. An operation that honours a mask takes it as an argument, exactly as it takes an
/// output view as an argument.
///
/// **A new mask selects nothing.** The allocation is zeroed, and a zero bit means the sample means
/// nothing, so a mask nobody has filled in yet selects no samples at all. The other direction would
/// make a half-written mask claim the samples it never reached.
///
/// What is here is what the model needs to be testable and no more: allocate, copy, combine, invert,
/// count, and build one from an image's own values under a value rule. Thresholding, binarising and
/// applying a mask to samples are operations on pixels rather than parts of the validity model, and
/// they live with the mask algorithms under `Algorithms/Masks/`.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace an
{

/// The engine context whose memory budget owns a mask. Defined in `Runtime/EngineContext.h`.
///
/// Named here rather than included, for the reason `Image/ImageStorage.h` names it: the image model
/// sits above the runtime, and a header that composes a mask should not inherit the runtime's whole
/// include closure to do it.
class EngineContext;

/// Where an engine context's memory comes from. Defined in `Runtime/MemoryResource.h`.
class MemoryResource;

/// How two masks are combined into a third.
///
/// Two answers, because two questions are asked of a pair of masks and they are not the same
/// question. Stacking asks which samples both frames agree about; regenerating a footprint asks
/// which samples either frame reached.
enum class ValidityCombination : std::uint8_t
{
    /// A sample means something only where both masks say it does. What a two-input operation whose
    /// result depends on both of them needs.
    Intersection,

    /// A sample means something where either mask says it does. What a coverage or footprint
    /// accumulation needs.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Union
};

/// Whether a value is one of the combinations this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param combination the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isValidityCombination(ValidityCombination combination) noexcept
{
    return static_cast<std::uint8_t>(combination) <= static_cast<std::uint8_t>(ValidityCombination::Union);
}

/// The combination's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param combination the combination to name. A value outside the enumeration is named
///        `"UnknownValidityCombination"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* validityCombinationName(ValidityCombination combination) noexcept;

/// A packed validity mask that owns its bits.
///
/// Move-only, because two owners of one allocation would release it twice. A default-constructed one
/// holds nothing, which is also what a move leaves behind and what `release` produces; `valid` is how
/// to ask, and every accessor answers for that state rather than requiring the question to have been
/// asked.
///
/// The mask holds its engine context open while it exists, for the reason a storage does: the bits
/// came from that context's resource and have to go back to it.
class ValidityMask
{
public:
    /// A mask that holds nothing.
    ///
    /// Allocates nothing, and is what a move and a `release` leave behind.
    AFTERNIGHTENGINE_API ValidityMask() noexcept;

    /// Gives the bits back to the resource they came from, and the lease back to the context.
    AFTERNIGHTENGINE_API ~ValidityMask();

    /// Takes over another mask's bits, leaving it holding nothing.
    ///
    /// Allocation-free. Pointers already handed out stay valid: the ownership moves and the bits do
    /// not.
    ///
    /// @param other the mask to take over. Left exactly as a default-constructed one.
    AFTERNIGHTENGINE_API ValidityMask(ValidityMask&& other) noexcept;

    /// Releases this mask's bits and takes over another's.
    ///
    /// The other mask's bits are taken over before this mask's are released, so a self-assignment
    /// keeps the memory rather than releasing it and then using what was released.
    ///
    /// @param other the mask to take over. Left exactly as a default-constructed one.
    /// @return this mask.
    AFTERNIGHTENGINE_API ValidityMask& operator=(ValidityMask&& other) noexcept;

    /// Copying is deleted: a mask is a real allocation and sharing one is a decision a call site
    /// should have to write. `clone` is the copy that says so.
    ValidityMask(const ValidityMask&) = delete;

    /// Copying is deleted. See the copy constructor.
    ValidityMask& operator=(const ValidityMask&) = delete;

    /// A mask of this shape, selecting nothing.
    ///
    /// The bits are zeroed, so the mask claims no sample means anything until something fills it in.
    ///
    /// @param engine the context whose memory resource and budget own the bits. It cannot shut down
    ///        while the mask lives.
    /// @param extent the mask's dimensions. Its channel count is how many bit-planes the mask has:
    ///        one for a mask shared by every channel of the image it covers, the image's channel
    ///        count for a mask that covers each channel separately.
    /// @return the mask; `ErrorCode::InvalidArgument` when the packed rows, the rows or the
    ///         bit-planes exceed the engine context's maximum width, height or channel count;
    ///         `ErrorCode::NotInitialized` for a context that holds nothing;
    ///         `ErrorCode::LimitExceeded` when the bytes exceed its maximum image size;
    ///         `ErrorCode::MemoryBudgetExceeded` when the configured budget refuses them, carrying
    ///         the request, what was available and the limit; or `ErrorCode::AllocationFailed` when
    ///         the machine does.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ValidityMask> create(const EngineContext& engine,
                                                                          const ImageExtent& extent);

    /// A mask of this shape, selecting everything.
    ///
    /// The named opposite of `create`, for the operation that starts from a fully valid frame and
    /// clears the samples it invalidates.
    ///
    /// @param engine the context whose memory resource and budget own the bits.
    /// @param extent the mask's dimensions.
    /// @return the mask, or whatever `create` reported.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ValidityMask> createAllValid(const EngineContext& engine,
                                                                                  const ImageExtent& extent);

    /// Whether this mask holds bits.
    ///
    /// Allocation-free.
    ///
    /// @return false for a default-constructed mask, one that has been moved from, and one that has
    ///         been released.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// The mask's dimensions.
    ///
    /// An optional rather than a reference because there is no extent of no mask.
    ///
    /// @return the extent, or an empty optional when this mask holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageExtent> extent() const noexcept;

    /// How many bytes this mask holds from its resource.
    ///
    /// The figure that counts against the configured memory budget for as long as the mask exists.
    ///
    /// @return the reserved bytes, or `0` when this mask holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t capacityBytes() const noexcept;

    /// Which engine context's budget these bits belong to.
    ///
    /// @return the owning context's identity, or `0` when this mask holds nothing. No live context
    ///         has an identity of zero.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t contextId() const noexcept;

    /// Which resource these bits came from and will go back to.
    ///
    /// @return the resource, or null when this mask holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API MemoryResource* memoryResource() const noexcept;

    /// A view of every bit, through which they may be changed.
    ///
    /// @return the view, or an empty optional when this mask holds nothing. It borrows this mask's
    ///         memory and must not outlive it.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ValidityMaskView<std::uint8_t>> view() noexcept;

    /// A view of every bit, through which none of them may be changed.
    ///
    /// @return the view, or an empty optional when this mask holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ValidityMaskView<const std::uint8_t>> view() const noexcept;

    /// A second mask holding a copy of these bits.
    ///
    /// The copy comes from the same engine context, so it is counted against the same budget and may
    /// be refused by it.
    ///
    /// @return the copy, or `ErrorCode::NotInitialized` when this mask holds nothing, or whatever
    ///         the allocation reported.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<ValidityMask> clone() const;

    /// Gives the bits back now rather than at the end of the scope.
    ///
    /// The explicit half of what the destructor does. Idempotent, and leaves the mask exactly as a
    /// default-constructed one.
    AFTERNIGHTENGINE_API void release() noexcept;

private:
    /// The allocation, the accounting and the hold on the engine context, defined only inside the
    /// engine so that the published surface says nothing about how a mask is laid out.
    class State;

    /// Takes ownership of a state the factory has already built.
    ///
    /// @param state the mask's memory and accounting. Never null on this path.
    explicit ValidityMask(std::unique_ptr<State> state) noexcept;

    /// What this mask holds, or null when it holds nothing.
    std::unique_ptr<State> m_state;
};

/// How many of a mask's pixels mean something, and how many there are.
///
/// The pair `Image/ValidityDescriptor.h`'s empty-selection rule is asked about, assembled here
/// because the count is a walk of the mask and the total is arithmetic.
///
/// Thread-safe with respect to the mask, which is only read. Allocation-free.
///
/// @param mask the mask to count.
/// @return the counts. A mask whose bits are all clear reports an empty selection, which is what
///         `requireSelection` turns into a failure.
[[nodiscard]] AFTERNIGHTENGINE_API ValidityCount countValidity(const ValidityMaskView<const std::uint8_t>& mask);

/// Whether a mask answers for the image an operation is about to read.
///
/// The pairing rule, stated once so that every operation taking an image and a mask reports it the
/// same way. A mask covers an image when it has the same width and height and either one bit-plane,
/// which every channel then shares, or one bit-plane per channel.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param mask the mask.
/// @param image the dimensions of the image the mask is to answer for.
/// @param operation the engine operation to name in the failure; null names none.
/// @return success when the mask covers the image; otherwise `ErrorCode::IncompatibleArguments`,
///         carrying both pixel counts when the width or the height differs, or both the image's
///         channel count and the mask's bit-plane count when the planes are neither one nor one
///         per channel.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkMaskCovers(const ValidityMaskView<const std::uint8_t>& mask,
                                                          const ImageExtent& image, const char* operation);

/// Copies one mask's bits into another of the same shape.
///
/// The bits past the last pixel of each destination row are written as zero whatever the source
/// held there.
///
/// @param destination where the bits go. Must not share memory with `source` unless it is exactly
///        the same mask, which makes the copy a no-op apart from those pad bits.
/// @param source where they come from.
/// @return success; `ErrorCode::IncompatibleArguments` when the two masks are not the same shape,
///         carrying both bit counts; or `ErrorCode::InvalidArgument` when they share memory without
///         being the same mask.
[[nodiscard]] AFTERNIGHTENGINE_API Status copyValidity(const ValidityMaskView<std::uint8_t>& destination,
                                                       const ValidityMaskView<const std::uint8_t>& source);

/// Combines two masks into a third.
///
/// The destination may be one of the sources, which is what makes an accumulation over many frames
/// one allocation rather than one per frame; it may not partially overlap one, because a partial
/// overlap reads bytes the same call has already written.
///
/// @param destination where the combined bits go.
/// @param left one source.
/// @param right the other.
/// @param combination whether a sample means something where both say so or where either does.
/// @return success; `ErrorCode::IncompatibleArguments` when the three masks are not the same shape;
///         `ErrorCode::InvalidArgument` for a combination outside the enumeration or for a
///         destination that partially overlaps a source.
[[nodiscard]] AFTERNIGHTENGINE_API Status combineValidity(const ValidityMaskView<std::uint8_t>& destination,
                                                          const ValidityMaskView<const std::uint8_t>& left,
                                                          const ValidityMaskView<const std::uint8_t>& right,
                                                          ValidityCombination combination);

/// Turns every meaningful sample of a mask into a meaningless one and back.
///
/// The bits past the last pixel of each row stay zero, so an inverted mask is still countable and
/// still comparable byte for byte.
///
/// @param mask the mask to invert, in place. There is no failure to report: every mask that exists
///        can be inverted, so this returns nothing.
AFTERNIGHTENGINE_API void invertValidity(const ValidityMaskView<std::uint8_t>& mask);

/// Writes into a mask which of an image's samples mean something under a value rule.
///
/// The bridge between the three rules a value can answer and the mask that carries a per-sample
/// answer, and the reason it exists here rather than with the mask algorithms: it is how the
/// compatibility rules are testable against real samples, and how an operation whose declared policy
/// combined two different rules materialises the mask that combination asked for.
///
/// A shared mask - one bit-plane over a multi-channel image - means something only where *every*
/// channel's sample does. That is the conservative direction and the one a footprint wants: a pixel
/// that one channel knows nothing about is not a pixel the result can use whole.
///
/// @tparam SampleT one of the eight resident sample types, or a const-qualified one of them.
/// @param mask where the answers go. Must cover `image`.
/// @param image the samples to judge.
/// @param validity the rule to judge them by. `ValidityRule::ExplicitMask` is refused: a mask cannot
///        be built from a rule that says a mask decides.
/// @return success; `ErrorCode::IncompatibleArguments` when the mask does not cover the image; or
///         `ErrorCode::InvalidArgument` when the rule is not one a value can answer.
template <ViewableSample SampleT>
[[nodiscard]] Status markValidity(const ValidityMaskView<std::uint8_t>& mask, const ImageView<SampleT>& image,
                                  const ValidityDescriptor& validity)
{
    const Status covers = checkMaskCovers(mask.constView(), image.extent(), "an::markValidity");
    if (covers.failed())
    {
        return covers;
    }
    if (!validityRuleIsDecidedByValue(validity.rule()))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "a mask cannot be built from a rule that says a mask decides",
                         .operation = "an::markValidity",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "validity")}});
    }

    const bool shared = mask.isShared();
    for (std::uint32_t maskPlane = 0u; maskPlane < mask.planeCount(); ++maskPlane)
    {
        for (std::uint32_t y = 0u; y < mask.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < mask.width(); ++x)
            {
                bool meaningful = true;
                for (std::uint32_t channel = 0u; channel < image.channelCount() && meaningful; ++channel)
                {
                    if (!shared && channel != maskPlane)
                    {
                        continue;
                    }
                    const std::uint32_t plane = channel / image.samplesPerPixel();
                    const std::uint32_t within = channel % image.samplesPerPixel();
                    const SampleT* const sample = image.sampleAt(plane, x, y, within);
                    meaningful = sample != nullptr && sampleIsValidByValue(validity, *sample);
                }
                (void)mask.setValid(maskPlane, x, y, meaningful);
            }
        }
    }
    return Status::success();
}

} // namespace an
