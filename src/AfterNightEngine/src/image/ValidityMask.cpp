// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ValidityMask.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

// A mask's bits are held in an ordinary image storage, described as bytes.
//
// That is worth saying out loud because the layout inside is not the mask's own geometry: the
// storage is told about a buffer whose width is the *bytes* one row packs into, and the mask's
// pixel width lives beside it. Both statements are true of the same memory, and keeping them apart
// is what stops a byte count being read as a pixel count. Everything the storage is for - taking
// the memory from the context's resource, counting it against the budget, holding the context open
// until it goes back - is the same problem an image's samples have and is already solved there.

namespace an
{
namespace
{

/// The byte geometry a mask of these dimensions is held in.
///
/// One byte-element per packed byte, one plane per bit-plane, tightly packed. The alignment is one
/// because nothing addresses a mask a vector at a time yet, and asking for more would round every
/// row up for no reader.
///
/// @param extent the mask's dimensions, in pixels.
/// @param limits the engine's configured maxima, which the byte extent is built against.
/// @return the request, or an empty optional when the byte geometry does not exist.
[[nodiscard]] std::optional<ImageLayoutRequest> maskByteRequest(const ImageExtent& extent,
                                                                const EngineLimits& limits) noexcept
{
    const std::uint64_t rowBytes = packedMaskRowBytes(extent.width());
    if (rowBytes > static_cast<std::uint64_t>(limits.maxImageWidth))
    {
        return std::nullopt;
    }
    const std::optional<ImageExtent> byteExtent =
        ImageExtent::create(static_cast<std::uint32_t>(rowBytes), extent.height(), extent.channelCount(), limits);
    if (!byteExtent.has_value())
    {
        return std::nullopt;
    }
    return ImageLayoutRequest{.extent = *byteExtent,
                              .elementSizeBytes = 1u,
                              .samplesPerPixel = 1u,
                              .planeCount = extent.channelCount(),
                              .rowAlignmentBytes = 1u};
}

/// Whether two masks describe the same number of bits in the same shape.
///
/// @param left one mask.
/// @param right the other.
/// @return true when width, height and bit-plane count all agree.
[[nodiscard]] bool sameShape(const ValidityMaskView<const std::uint8_t>& left,
                             const ValidityMaskView<const std::uint8_t>& right) noexcept
{
    return left.width() == right.width() && left.height() == right.height() && left.planeCount() == right.planeCount();
}

/// The failure two masks of different shapes produce.
///
/// @param operation the engine operation to name.
/// @param left one mask.
/// @param right the other.
/// @return `ErrorCode::IncompatibleArguments`, carrying both bit counts.
[[nodiscard]] Status shapeMismatch(const char* operation, const ValidityMaskView<const std::uint8_t>& left,
                                   const ValidityMaskView<const std::uint8_t>& right)
{
    return Status::failure(
        ErrorCode::IncompatibleArguments,
        StatusDetail{.message = "these masks do not answer for the same pixels",
                     .operation = operation != nullptr ? operation : "",
                     .domain = ErrorDomain::Image,
                     .context = {StatusContextField::ofCount(ContextKey::Requested, left.bitCount()),
                                 StatusContextField::ofCount(ContextKey::Available, right.bitCount())}});
}

/// Whether one mask overlaps another without being exactly it.
///
/// Exactly it is legitimate and is what an accumulation does; a partial overlap is not, because the
/// bytes a call has already written would be read as input further along the same row.
///
/// @param destination the mask being written.
/// @param source a mask being read.
/// @return true when the two share memory and are not the same view.
[[nodiscard]] bool partiallyOverlaps(const ValidityMaskView<std::uint8_t>& destination,
                                     const ValidityMaskView<const std::uint8_t>& source) noexcept
{
    if (static_cast<const void*>(destination.data()) == static_cast<const void*>(source.data()) &&
        destination.rowStrideBytes() == source.rowStrideBytes() &&
        destination.planeStrideBytes() == source.planeStrideBytes() && sameShape(destination.constView(), source))
    {
        return false;
    }
    return regionsOverlap(destination.data(), destination.spannedByteCount(), source.data(), source.spannedByteCount());
}

} // namespace

/// The memory a mask holds, and the geometry that says what its bits mean.
class ValidityMask::State
{
public:
    /// Builds a state over storage that has already been taken.
    ///
    /// @param bits the memory, whose byte layout matches this extent.
    /// @param extent the mask's dimensions, in pixels.
    State(ImageStorageBytes&& bits, const ImageExtent& extent) noexcept : m_bits(std::move(bits)), m_extent(extent) {}

    /// The memory the bits sit in.
    ///
    /// @return the storage.
    [[nodiscard]] ImageStorageBytes& bits() noexcept
    {
        return m_bits;
    }

    /// The memory the bits sit in, without permission to change it.
    ///
    /// @return the storage.
    [[nodiscard]] const ImageStorageBytes& bits() const noexcept
    {
        return m_bits;
    }

    /// The mask's dimensions.
    ///
    /// @return the extent, in pixels.
    [[nodiscard]] const ImageExtent& extent() const noexcept
    {
        return m_extent;
    }

private:
    /// The bytes, counted against the engine context that owns them.
    ImageStorageBytes m_bits;

    /// What those bytes mean, in pixels rather than in bytes.
    ImageExtent m_extent;
};

ValidityMask::ValidityMask() noexcept = default;

ValidityMask::~ValidityMask() = default;

ValidityMask::ValidityMask(ValidityMask&& other) noexcept = default;

ValidityMask& ValidityMask::operator=(ValidityMask&& other) noexcept = default;

ValidityMask::ValidityMask(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

Result<ValidityMask> ValidityMask::create(const EngineContext& engine, const ImageExtent& extent)
{
    if (!engine.valid())
        return Status::failure(ErrorCode::NotInitialized,
                               {.message = "a validity mask needs an engine context that holds state",
                                .operation = "ValidityMask::create",
                                .domain = ErrorDomain::Image});
    const std::optional<ImageLayoutRequest> request = maskByteRequest(extent, engine.limits());
    if (!request.has_value())
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "a mask of these dimensions has no byte geometry under the limits in force",
                         .operation = "ValidityMask::create",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, extent.sampleCount())}});
    }

    Result<ImageStorageBytes> bits = ImageStorageBytes::create(engine, *request, StorageInitialization::Zeroed);
    if (!bits.hasValue())
    {
        return bits.status();
    }
    return ValidityMask{std::make_unique<State>(std::move(*bits), extent)};
}

Result<ValidityMask> ValidityMask::createAllValid(const EngineContext& engine, const ImageExtent& extent)
{
    Result<ValidityMask> mask = create(engine, extent);
    if (!mask.hasValue())
    {
        return mask;
    }
    const std::optional<ValidityMaskView<std::uint8_t>> writable = mask->view();
    if (!writable.has_value())
    {
        return Status::failure(ErrorCode::Internal, StatusDetail{.message = "a mask that was just taken has no view",
                                                                 .operation = "ValidityMask::createAllValid",
                                                                 .domain = ErrorDomain::Image});
    }
    writable->fill(true);
    return mask;
}

bool ValidityMask::valid() const noexcept
{
    return m_state != nullptr && m_state->bits().valid();
}

std::optional<ImageExtent> ValidityMask::extent() const noexcept
{
    if (!valid())
    {
        return std::nullopt;
    }
    return m_state->extent();
}

std::uint64_t ValidityMask::capacityBytes() const noexcept
{
    return m_state != nullptr ? m_state->bits().capacityBytes() : 0u;
}

std::uint64_t ValidityMask::contextId() const noexcept
{
    return m_state != nullptr ? m_state->bits().contextId() : 0u;
}

MemoryResource* ValidityMask::memoryResource() const noexcept
{
    return m_state != nullptr ? m_state->bits().memoryResource() : nullptr;
}

std::optional<ValidityMaskView<std::uint8_t>> ValidityMask::view() noexcept
{
    if (!valid())
    {
        return std::nullopt;
    }
    const std::optional<ImageLayout> layout = m_state->bits().layout();
    if (!layout.has_value())
    {
        return std::nullopt;
    }
    return ValidityMaskView<std::uint8_t>::createChecked(static_cast<std::uint8_t*>(m_state->bits().data()),
                                                         m_state->extent(), layout->rowStrideBytes(),
                                                         layout->planeStrideBytes());
}

std::optional<ValidityMaskView<const std::uint8_t>> ValidityMask::view() const noexcept
{
    if (!valid())
    {
        return std::nullopt;
    }
    const std::optional<ImageLayout> layout = m_state->bits().layout();
    if (!layout.has_value())
    {
        return std::nullopt;
    }
    return ValidityMaskView<const std::uint8_t>::createChecked(static_cast<const std::uint8_t*>(m_state->bits().data()),
                                                               m_state->extent(), layout->rowStrideBytes(),
                                                               layout->planeStrideBytes());
}

Result<ValidityMask> ValidityMask::clone() const
{
    if (!valid())
    {
        return Status::failure(ErrorCode::NotInitialized, StatusDetail{.message = "this mask holds nothing to copy",
                                                                       .operation = "ValidityMask::clone",
                                                                       .domain = ErrorDomain::Image});
    }
    Result<ImageStorageBytes> copied = m_state->bits().clone();
    if (!copied.hasValue())
    {
        return copied.status();
    }
    return ValidityMask{std::make_unique<State>(std::move(*copied), m_state->extent())};
}

void ValidityMask::release() noexcept
{
    m_state.reset();
}

const char* validityCombinationName(ValidityCombination combination) noexcept
{
    switch (combination)
    {
    case ValidityCombination::Intersection:
        return "Intersection";
    case ValidityCombination::Union:
        return "Union";
    }
    return "UnknownValidityCombination";
}

ValidityCount countValidity(const ValidityMaskView<const std::uint8_t>& mask)
{
    return ValidityCount{.valid = mask.countValid(), .total = mask.bitCount()};
}

Status checkMaskCovers(const ValidityMaskView<const std::uint8_t>& mask, const ImageExtent& image,
                       const char* operation)
{
    if (mask.width() != image.width() || mask.height() != image.height())
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the mask does not answer for the same pixels as the image",
                         .operation = operation != nullptr ? operation : "",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, image.pixelCount()),
                                     StatusContextField::ofCount(ContextKey::Available,
                                                                 static_cast<std::uint64_t>(mask.width()) *
                                                                     static_cast<std::uint64_t>(mask.height()))}});
    }
    if (mask.planeCount() != 1u && mask.planeCount() != image.channelCount())
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "a mask answers for one channel each or for every channel at once, and this one "
                                    "has neither as many bit-planes as the image has channels nor exactly one",
                         .operation = operation != nullptr ? operation : "",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, image.channelCount()),
                                     StatusContextField::ofCount(ContextKey::Available, mask.planeCount())}});
    }
    return Status::success();
}

Status copyValidity(const ValidityMaskView<std::uint8_t>& destination,
                    const ValidityMaskView<const std::uint8_t>& source)
{
    if (!sameShape(destination.constView(), source))
    {
        return shapeMismatch("an::copyValidity", destination.constView(), source);
    }
    if (partiallyOverlaps(destination, source))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "the destination mask shares bytes with the source without being it",
                         .operation = "an::copyValidity",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "destination")}});
    }

    const std::uint8_t last = packedMaskLastByteMask(destination.width());
    for (std::uint32_t plane = 0u; plane < destination.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < destination.height(); ++y)
        {
            const std::span<std::uint8_t> target = *destination.row(plane, y);
            const std::span<const std::uint8_t> from = *source.row(plane, y);
            for (std::size_t index = 0u; index < target.size(); ++index)
            {
                target[index] = from[index];
            }
            target.back() = static_cast<std::uint8_t>(target.back() & last);
        }
    }
    return Status::success();
}

Status combineValidity(const ValidityMaskView<std::uint8_t>& destination,
                       const ValidityMaskView<const std::uint8_t>& left,
                       const ValidityMaskView<const std::uint8_t>& right, ValidityCombination combination)
{
    if (!isValidityCombination(combination))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "the validity combination is not one this engine knows",
                         .operation = "an::combineValidity",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "combination")}});
    }
    if (!sameShape(destination.constView(), left))
    {
        return shapeMismatch("an::combineValidity", destination.constView(), left);
    }
    if (!sameShape(destination.constView(), right))
    {
        return shapeMismatch("an::combineValidity", destination.constView(), right);
    }
    if (partiallyOverlaps(destination, left) || partiallyOverlaps(destination, right))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "the destination mask shares bytes with a source without being it",
                         .operation = "an::combineValidity",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "destination")}});
    }

    const std::uint8_t last = packedMaskLastByteMask(destination.width());
    for (std::uint32_t plane = 0u; plane < destination.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < destination.height(); ++y)
        {
            const std::span<std::uint8_t> target = *destination.row(plane, y);
            const std::span<const std::uint8_t> a = *left.row(plane, y);
            const std::span<const std::uint8_t> b = *right.row(plane, y);
            for (std::size_t index = 0u; index < target.size(); ++index)
            {
                target[index] = combination == ValidityCombination::Intersection
                                    ? static_cast<std::uint8_t>(a[index] & b[index])
                                    : static_cast<std::uint8_t>(a[index] | b[index]);
            }
            target.back() = static_cast<std::uint8_t>(target.back() & last);
        }
    }
    return Status::success();
}

void invertValidity(const ValidityMaskView<std::uint8_t>& mask)
{
    const std::uint8_t last = packedMaskLastByteMask(mask.width());
    for (std::uint32_t plane = 0u; plane < mask.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < mask.height(); ++y)
        {
            const std::span<std::uint8_t> target = *mask.row(plane, y);
            for (std::uint8_t& byte : target)
            {
                byte = static_cast<std::uint8_t>(~byte);
            }
            target.back() = static_cast<std::uint8_t>(target.back() & last);
        }
    }
}

} // namespace an
