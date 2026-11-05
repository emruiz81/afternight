// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Describing engine samples as a G'MIC image, and G'MIC's as engine ones.
///
/// The C bridge into G'MIC describes an image as a base pointer, four unsigned dimensions - width,
/// height, depth and spectrum - a flag saying whether a pixel's samples are adjacent, and a two-value
/// representation code. There is no stride anywhere in it: the library's own image is strictly tight,
/// so `data` is `width * height * depth * spectrum` samples with nothing between a row and the next
/// one and nothing between a plane and the next one. Every one of those is a fact this adapter can
/// check before anything reaches the library, which is the point, because the failure a bridge
/// produces is not a compile error. It is a filter that quietly reads the padding between an image's
/// rows as though it were picture.
///
/// That absent stride is the substance of this header rather than a detail of it. Samples handed
/// straight to the library because they are already float, with nobody asking whether the rows are
/// padded, are the defect it prevents: this header refuses a padded layout by name, and the caller
/// repacks through the engine's own conversion, where the copy is visible in a profile.
///
/// No G'MIC or CImg header is included here and no type of either appears in this surface. The
/// description below carries the fields the library's own structure carries, in the types it uses, so
/// a caller that has the library fills its structure from a checked description in one statement and
/// a caller that does not pays nothing.
///
/// **Float and planar, both ways, and nothing else.** The library's interface names a byte
/// representation as well as a float one, and it will interleave a result on request. Both of those
/// are conversions the library performs on the way in or out, and a conversion in this engine is five
/// named decisions rather than a flag on somebody else's structure - so this adapter states float
/// planes in both directions and refuses anything else by name. An engine image of another
/// representation is converted through `Image/ImageConversion.h` first, where the rounding, range and
/// domain rules are the caller's to state. That is also what "a direct Float32 view" means here: for
/// a tightly packed planar float image there is nothing between the engine's samples and the
/// library's, and for everything else there is an engine conversion the caller wrote.
///
/// The contract half of the bridge is the engine's own `Interop/ExternalBuffer.h` vocabulary, used
/// unchanged. An adapter outside the engine binary shares the refusal alphabet rather than inventing
/// a parallel one, so a caller that already branches on `BridgeRefusal` for OpenCV branches on the
/// same values here.

#pragma once

#include <AfterNightEngineGmicAdapter/Api.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <cstdint>
#include <optional>
#include <type_traits>

namespace an
{

/// The representations the library's own interface names for one sample.
///
/// Both enumerators exist because the library's structure can carry either, and a result has to be
/// checked against what was asked for rather than assumed. Only `Float32` is ever produced by this
/// adapter: see the header comment for why the byte representation is a conversion rather than a
/// format this engine hands over.
///
/// The values are the library's own, so a caller assigns them straight across into its structure's
/// representation field.
enum class GmicPixelFormat : std::int32_t
{
    /// `E_FORMAT_FLOAT`: one single-precision float per sample, which is what the library computes
    /// in.
    Float32 = 0,

    /// `E_FORMAT_BYTE`: one unsigned byte per sample, which the library converts to and from float
    /// at its own boundary.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    UInt8 = 1
};

/// The representation this adapter hands over and expects back.
///
/// Named rather than repeated at each use, because it is one decision, stated in the header comment,
/// and not four coincidences.
inline constexpr SampleType kGmicSampleType = SampleType::Float32;

/// The longest image name the library's own structure holds, not counting the terminator.
///
/// `MAX_IMAGE_NAME_LENGTH` in its header. A name this adapter sends is truncated to it. The upstream
/// bridge copies a name back with no bound, so `GmicRuntimeAdapter` asks this repository's build of
/// it to refuse a longer name, reserves room after the field in case one is copied anyway, and
/// reports `GmicAdapterReason::ResultNameOverflowed` for either rather than trusting the result.
inline constexpr std::uint32_t kGmicMaxImageNameLength = 255u;

/// One image's worth of description: everything the library's structure needs to address samples
/// somebody else owns, and nothing it would compute for itself.
///
/// The field types are the ones that structure uses - a `void*` base and `unsigned int` dimensions -
/// so a caller assigns them across without a cast that could truncate. The base pointer is `void*`
/// and its constness lives in the contract that produced the description and in `readOnly`, exactly
/// as it does there.
///
/// There is no stride and no name. The first is absent because the library has none; the second
/// because a name is a property of a call rather than of the memory a call is given.
struct GmicImageDescription
{
    /// The first sample. Never null in a description this adapter produced.
    void* data;

    /// How many samples one row holds.
    std::uint32_t width;

    /// How many rows one plane holds.
    std::uint32_t height;

    /// How many slices the image has along the library's third axis. Always one here: this engine
    /// describes plane images, and a volume is refused rather than flattened.
    std::uint32_t depth;

    /// How many planes the image has, which is the library's `spectrum` and this engine's channel
    /// count for planar storage.
    std::uint32_t spectrum;

    /// Whether a pixel's samples are adjacent. Always false here, because the library interleaves by
    /// transposing its own copy and this adapter does not ask it to.
    bool interleaved;

    /// What one sample is. Always `GmicPixelFormat::Float32` in a description this adapter produced.
    GmicPixelFormat format;

    /// Whether the borrower may only read. A description built over const samples or for a read-only
    /// contract is read-only, and the library may then be given only a copy of the samples:
    /// `GmicSourceSharing::Share` over it is refused with `GmicAdapterReason::SourceNotShareable`.
    bool readOnly;

    /// Whether two descriptions describe the same image.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend constexpr bool operator==(const GmicImageDescription& left,
                                                   const GmicImageDescription& right) noexcept = default;
};

/// Whether a value is one of the representations the library's interface names.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isGmicPixelFormat(GmicPixelFormat format) noexcept
{
    return format == GmicPixelFormat::Float32 || format == GmicPixelFormat::UInt8;
}

/// The representation's own name, for a failure message or a diagnostic field.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the representation to name. A value outside the enumeration is named
///        `"UnknownGmicPixelFormat"`.
/// @return a null-terminated string literal, which outlives every caller and is never freed.
[[nodiscard]] constexpr const char* gmicPixelFormatName(GmicPixelFormat format) noexcept
{
    switch (format)
    {
    case GmicPixelFormat::Float32:
        return "Float32";
    case GmicPixelFormat::UInt8:
        return "UInt8";
    }
    return "UnknownGmicPixelFormat";
}

/// How many samples an image of these dimensions holds, or none when the product does not fit.
///
/// The library's own size arithmetic, done once here in 64 bits so that no call site multiplies four
/// 32-bit dimensions together and wraps.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param description the image, as its own fields report it.
/// @return the sample count, or an empty optional when a dimension is zero or the product exceeds
///         what an unsigned 64-bit count holds.
[[nodiscard]] constexpr std::optional<std::uint64_t> gmicSampleCount(const GmicImageDescription& description) noexcept
{
    if (description.width == 0u || description.height == 0u || description.depth == 0u || description.spectrum == 0u)
    {
        return std::nullopt;
    }
    constexpr std::uint64_t kCeiling = ~std::uint64_t{0};
    std::uint64_t samples = description.width;
    for (const std::uint64_t dimension :
         {static_cast<std::uint64_t>(description.height), static_cast<std::uint64_t>(description.depth),
          static_cast<std::uint64_t>(description.spectrum)})
    {
        if (samples > kCeiling / dimension)
        {
            return std::nullopt;
        }
        samples *= dimension;
    }
    return samples;
}

/// How many bytes an image of these dimensions occupies, or none when it cannot be counted.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param description the image, as its own fields report it. Its representation decides the sample
///        width, and a representation outside the enumeration has none.
/// @return the byte count, or an empty optional when the sample count does not exist or the byte
///         count would exceed what an unsigned 64-bit count holds.
[[nodiscard]] constexpr std::optional<std::uint64_t> gmicByteCount(const GmicImageDescription& description) noexcept
{
    if (!isGmicPixelFormat(description.format))
    {
        return std::nullopt;
    }
    const std::optional<std::uint64_t> samples = gmicSampleCount(description);
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    const std::uint64_t sampleSize = description.format == GmicPixelFormat::Float32 ? 4u : 1u;
    constexpr std::uint64_t kCeiling = ~std::uint64_t{0};
    if (*samples > kCeiling / sampleSize)
    {
        return std::nullopt;
    }
    return *samples * sampleSize;
}

/// Whether an image layout can be described to the library at all, before any claim is looked at.
///
/// The layout half of the bridge, and the half that decides whether there is a copy. The order is a
/// contract: membership of the closed sets first, then the representation, then the arrangement,
/// then the dimensions, then the packing, so a layout with two problems reports the same one on
/// every build.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type what one sample is. Only `SampleType::Float32` is describable; see the header
///        comment.
/// @param layout where the samples sit.
/// @return `BridgeRefusal::None` when an image can be described over those samples; otherwise the
///         first rule that refused. `UnknownContract` for a type outside the enumeration;
///         `SampleTypeNotRepresentable` for a representation other than float, or an element size
///         that is not a float's; `ArrangementNotRepresentable` for interleaved or semi-planar
///         storage, and for planes that are not consecutive; `ExtentExceedsLibraryIndex` for a
///         dimension or a channel count above what an `unsigned int` holds;
///         `RowStrideNotRepresentable` for a row that is padded, which the library has no field to
///         express.
[[nodiscard]] constexpr BridgeRefusal checkGmicImageGeometry(SampleType type, const ImageLayout& layout) noexcept
{
    if (!isSampleType(type))
    {
        return BridgeRefusal::UnknownContract;
    }
    if (type != kGmicSampleType || layout.elementSizeBytes() != sampleTypeSizeBytes(type))
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    if (storageArrangementOf(layout) != StorageArrangement::Planar)
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    constexpr std::uint64_t kMaxDimension = 0xffffffffu;
    if (layout.extent().width() > kMaxDimension || layout.extent().height() > kMaxDimension ||
        layout.planeCount() > kMaxDimension)
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }
    const std::uint64_t packedRow = static_cast<std::uint64_t>(layout.extent().width()) * layout.elementSizeBytes();
    if (layout.rowStrideBytes() != packedRow)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    if (layout.planeStrideBytes() != packedRow * layout.extent().height())
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    return BridgeRefusal::None;
}

/// Whether this image can be handed to the library, and whether it can be handed over directly.
///
/// The whole bridge: the claim and then the layout, in that order, so a refusal about what the
/// samples mean is never hidden behind one about how they sit.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT the image's sample type; a const one describes read-only samples.
/// @param image the samples to hand over.
/// @param contract what the bridge is being told about them.
/// @return `BridgeRefusal::None` when the samples can be handed over; otherwise the first rule that
///         refused.
template <ViewableSample SampleT>
[[nodiscard]] constexpr BridgeRefusal checkGmicImage(const ImageView<SampleT>& image,
                                                     const ExternalBufferContract& contract) noexcept
{
    const BridgeRefusal claim = checkExternalBufferContract(contract, !std::is_const_v<SampleT>);
    if (claim != BridgeRefusal::None)
    {
        return claim;
    }
    if (image.data() == nullptr)
    {
        return BridgeRefusal::NoSamples;
    }
    return checkGmicImageGeometry(ImageView<SampleT>::kSampleType, image.layout());
}

/// The image description for one engine image, when the bridge holds.
///
/// The caller fills the library's structure from what comes back, and every field of it has been
/// checked. When the answer is empty, `checkGmicImage` names the rule.
///
/// The dimensions are the engine's own read into the library's four: width and height are the
/// extent, depth is one because this engine describes plane images, and spectrum is the plane count.
/// Nothing is transposed and nothing is permuted, which is what makes a tightly packed planar float
/// image describable with no copy at all.
///
/// The description borrows: it points at the image's samples and owns nothing, so it must not
/// outlive them, which is what `BorrowScope` in the contract is for.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT the image's sample type; a const one describes read-only samples.
/// @param image the samples to hand over.
/// @param contract what the bridge is being told about them.
/// @return the description, or an empty optional when any rule refuses.
template <ViewableSample SampleT>
[[nodiscard]] constexpr std::optional<GmicImageDescription>
describeAsGmicImage(const ImageView<SampleT>& image, const ExternalBufferContract& contract) noexcept
{
    if (checkGmicImage(image, contract) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    const ImageLayout& layout = image.layout();
    GmicImageDescription description{};
    description.data = const_cast<std::remove_const_t<SampleT>*>(image.data());
    description.width = layout.extent().width();
    description.height = layout.extent().height();
    description.depth = 1u;
    description.spectrum = layout.planeCount();
    description.interleaved = false;
    description.format = GmicPixelFormat::Float32;
    description.readOnly = std::is_const_v<SampleT> || contract.mutability == BufferMutability::ReadOnly;
    return description;
}

/// Whether a description names an image both this engine and the library can address.
///
/// The description half of the bridge, and it is asked in both directions: of a description this
/// adapter produced before it is handed over, and of one the library wrote back. The rules are the
/// same either way, because what they judge is what the library is free to change about an image
/// while a command runs - its representation, its interleaving, its third dimension and its size -
/// and a caller may hand over a description it built rather than one `describeAsGmicImage` returned.
///
/// The library's buffer is tightly packed by construction, so no stride appears here.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param description the image, as the library's own structure reports it.
/// @return `BridgeRefusal::None` when a layout can be built over it; otherwise the first rule that
///         refused. `UnknownContract` for a representation outside the enumeration; `NoSamples` for
///         a null base; `SampleTypeNotRepresentable` for the byte representation;
///         `ArrangementNotRepresentable` for an interleaved buffer or a depth above one;
///         `ExtentExceedsLibraryIndex` for a zero dimension or a size this engine cannot count.
[[nodiscard]] constexpr BridgeRefusal checkGmicImageDescription(const GmicImageDescription& description) noexcept
{
    if (!isGmicPixelFormat(description.format))
    {
        return BridgeRefusal::UnknownContract;
    }
    if (description.data == nullptr)
    {
        return BridgeRefusal::NoSamples;
    }
    if (description.format != GmicPixelFormat::Float32)
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    if (description.interleaved || description.depth != 1u)
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (!gmicByteCount(description).has_value())
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }
    return BridgeRefusal::None;
}

/// The engine layout an image the library produced already has.
///
/// What makes the result path explicit rather than implied: the library's buffer is tight, so the
/// layout that describes it has packed rows and consecutive planes, and a caller that receives one
/// reads it with the engine's own view types rather than with pointer arithmetic of its own.
///
/// Thread-safe and allocation-free.
///
/// @param description the image, as the library's own structure reports it.
/// @param limits the geometry limits in force, which the resulting extent is checked against.
/// @return the layout, or an empty optional when `checkGmicImageDescription` refuses the description
///         or the extent is outside the limits.
[[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API std::optional<ImageLayout>
gmicResultLayout(const GmicImageDescription& description, const EngineLimits& limits) noexcept;

} // namespace an
