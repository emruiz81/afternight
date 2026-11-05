// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Describing engine samples as an OpenCV matrix, and OpenCV's as engine ones.
///
/// `cv::Mat` is a two-dimensional row-major buffer whose channels are interleaved within a row, with
/// a base pointer, a row step in bytes, `int` dimensions, and one packed integer that carries both
/// the element depth and the channel count. Every one of those is a fact this engine can check
/// before anything constructs the matrix - which is the point, because the failure a bridge produces
/// is not a compile error. It is a matrix that quietly describes the wrong pixels.
///
/// No OpenCV header is included here, and no `cv::Mat` appears in this engine's surface. The
/// description below carries the fields OpenCV's own structure carries, in the types it uses, so a
/// caller that has OpenCV builds the matrix from a checked description in one statement and a caller
/// that does not pays nothing. The packed type code is the one piece of OpenCV's ABI this engine has
/// to know, so it is not assumed: `OpenCvTypeLayout` is handed the library's own constants, every
/// type-code function here computes with them, and `checkOpenCvTypeLayout` refuses a packing that
/// cannot hold the depths or depth constants that differ from this engine's table - which is a real
/// risk rather than a theoretical one, because the channel shift is a different number in the next
/// major version of OpenCV.
///
/// What the checks come to, in practice, is that a zero-copy `cv::Mat` over engine samples exists for
/// a single plane of a representable depth, and not for a multi-channel image: this engine's storage
/// is planar and a matrix's channels are interleaved, so the two describe different byte orders for
/// the same pixels. That is not a limitation being worked around: a multi-channel matrix is an
/// allocation and a copy, and it is made through a conversion that says so.
///
/// The depth table holds only depths that keep a sample's number and representation, so a matrix
/// never holds different numbers than the image did without the call site knowing. Unsigned 32-bit
/// has no OpenCV depth and says so by name; the caller converts explicitly or does something else.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace an
{

/// The element depths OpenCV defines, by the values its own headers give them.
///
/// An enumeration rather than loose integers so a depth cannot be passed where a channel count
/// belongs. The values are OpenCV's, and `checkOpenCvTypeLayout` is how a build proves they are
/// still its values rather than this engine's memory of them.
enum class OpenCvDepth : std::int32_t
{
    /// `CV_8U`: unsigned 8-bit.
    Unsigned8 = 0,

    /// `CV_8S`: signed 8-bit.
    Signed8 = 1,

    /// `CV_16U`: unsigned 16-bit.
    Unsigned16 = 2,

    /// `CV_16S`: signed 16-bit.
    Signed16 = 3,

    /// `CV_32S`: signed 32-bit. OpenCV has no unsigned 32-bit depth, which is why
    /// `SampleType::UInt32` has no matrix representation.
    Signed32 = 4,

    /// `CV_32F`: single-precision floating point.
    Float32 = 5,

    /// `CV_64F`: double-precision floating point.
    ///
    /// Deliberately the last enumerator this engine uses; OpenCV defines `CV_16F` above it, and no
    /// engine sample type maps to a half.
    Float64 = 6
};

/// How OpenCV packs a depth and a channel count into the single integer `cv::Mat::type()` returns.
///
/// Handed to this engine by whoever has the OpenCV headers, rather than assumed, because the
/// packing is version-specific: OpenCV 4 shifts the channel count by three bits and OpenCV 5 by a
/// different number. A caller fills this from `CV_CN_SHIFT`, `CV_MAT_DEPTH_MASK` and `CV_CN_MAX`
/// and passes it to every call here, so a build against a library whose packing changed fails a
/// check instead of producing a matrix that claims a different depth than it holds.
struct OpenCvTypeLayout
{
    /// `CV_CN_SHIFT`: how far left the channel count sits above the depth.
    std::int32_t channelShift;

    /// `CV_MAT_DEPTH_MASK`: the bits of a type code that hold the depth.
    std::int32_t depthMask;

    /// `CV_CN_MAX`: the largest channel count a matrix may have.
    std::int32_t maxChannels;

    /// Whether two layouts describe the same packing.
    ///
    /// @param left one layout.
    /// @param right the other.
    /// @return true when all three fields are equal.
    [[nodiscard]] friend constexpr bool operator==(const OpenCvTypeLayout& left,
                                                   const OpenCvTypeLayout& right) noexcept = default;
};

/// The packing OpenCV 4 uses, which is what this repository builds against.
///
/// A default rather than an assumption: it is the value a caller gets when it has no reason to
/// think otherwise, and `checkOpenCvTypeLayout` is what turns a wrong assumption into a refusal at
/// the bridge instead of a wrong matrix at the call site.
inline constexpr OpenCvTypeLayout kOpenCvTypeLayout4{.channelShift = 3, .depthMask = 7, .maxChannels = 512};

/// One `cv::Mat` header's worth of description: everything needed to build a matrix over samples
/// somebody else owns, and nothing OpenCV would compute for itself.
///
/// The field types are the ones OpenCV's own structure uses - `int` rows and columns, a `size_t`
/// step - so a caller assigns them across without a cast that could truncate. The base pointer is
/// `void*` and its constness lives in the contract that produced this description, exactly as it
/// does in `cv::Mat`, which has no const-carrying variant either.
struct OpenCvMatDescription
{
    /// The first sample. Never null in a description this engine produced.
    void* data;

    /// `cv::Mat::rows`: the number of rows.
    std::int32_t rows;

    /// `cv::Mat::cols`: the number of columns, in pixels rather than samples.
    std::int32_t cols;

    /// `cv::Mat::type()`: the packed depth and channel count, computed with the caller's own
    /// `OpenCvTypeLayout`.
    std::int32_t type;

    /// `cv::Mat::step[0]`: the distance between the first samples of consecutive rows, in bytes.
    std::size_t stepBytes;

    /// Whether two descriptions describe the same matrix.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when all five fields are equal.
    [[nodiscard]] friend constexpr bool operator==(const OpenCvMatDescription& left,
                                                   const OpenCvMatDescription& right) noexcept = default;
};

/// Whether a value is one of the depths this engine maps to.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param depth the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOpenCvDepth(OpenCvDepth depth) noexcept
{
    return static_cast<std::int32_t>(depth) >= static_cast<std::int32_t>(OpenCvDepth::Unsigned8) &&
           static_cast<std::int32_t>(depth) <= static_cast<std::int32_t>(OpenCvDepth::Float64);
}

/// Whether a type layout is one a matrix could actually use.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param layout the packing to test.
/// @return true when the shift is positive, the mask covers every depth this engine maps to and
///         lies wholly below the channel field, and the channel maximum is at least one.
[[nodiscard]] constexpr bool isOpenCvTypeLayout(const OpenCvTypeLayout& layout) noexcept
{
    return layout.channelShift > 0 && layout.depthMask >= static_cast<std::int32_t>(OpenCvDepth::Float64) &&
           layout.maxChannels >= 1 && (layout.depthMask >> layout.channelShift) == 0;
}

/// The OpenCV depth that holds an engine sample type without changing what it is.
///
/// A closed table, and the two gaps in it are the whole point. OpenCV defines no unsigned 32-bit
/// depth, so `SampleType::UInt32` has no answer here: filling that gap with `CV_64F` would change
/// both the representation and the numbers, a conversion wearing the name of a cast.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the engine representation.
/// @return the depth that holds exactly those bits, or an empty optional when OpenCV has none.
[[nodiscard]] constexpr std::optional<OpenCvDepth> openCvDepthOf(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::UInt8:
        return OpenCvDepth::Unsigned8;
    case SampleType::Int8:
        return OpenCvDepth::Signed8;
    case SampleType::UInt16:
        return OpenCvDepth::Unsigned16;
    case SampleType::Int16:
        return OpenCvDepth::Signed16;
    case SampleType::UInt32:
        return std::nullopt;
    case SampleType::Int32:
        return OpenCvDepth::Signed32;
    case SampleType::Float32:
        return OpenCvDepth::Float32;
    case SampleType::Float64:
        return OpenCvDepth::Float64;
    }
    return std::nullopt;
}

/// The engine representation an OpenCV depth holds.
///
/// The other direction of the same table. `openCvDepthOf(*openCvSampleTypeOf(depth)) == depth` for
/// every depth this engine maps, which the suite asserts rather than assumes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param depth the OpenCV depth.
/// @return the representation those bits already are, or an empty optional for a depth outside the
///         table - `CV_16F` among them, which no engine sample type matches.
[[nodiscard]] constexpr std::optional<SampleType> openCvSampleTypeOf(OpenCvDepth depth) noexcept
{
    switch (depth)
    {
    case OpenCvDepth::Unsigned8:
        return SampleType::UInt8;
    case OpenCvDepth::Signed8:
        return SampleType::Int8;
    case OpenCvDepth::Unsigned16:
        return SampleType::UInt16;
    case OpenCvDepth::Signed16:
        return SampleType::Int16;
    case OpenCvDepth::Signed32:
        return SampleType::Int32;
    case OpenCvDepth::Float32:
        return SampleType::Float32;
    case OpenCvDepth::Float64:
        return SampleType::Float64;
    }
    return std::nullopt;
}

/// The packed type code for a depth and a channel count, under the caller's own packing.
///
/// The engine's arithmetic replacement for `CV_MAKETYPE`. It is computed rather than looked up so
/// that a channel count OpenCV allows and this engine has never seen still produces the right code.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param depth the element depth.
/// @param channels how many channels one pixel has; at least one and at most `layout.maxChannels`.
/// @param layout the packing the caller's OpenCV uses.
/// @return the code, or an empty optional when the depth, the channel count or the packing is
///         outside what a matrix can express.
[[nodiscard]] constexpr std::optional<std::int32_t> openCvTypeCode(OpenCvDepth depth, std::int32_t channels,
                                                                   const OpenCvTypeLayout& layout) noexcept
{
    if (!isOpenCvDepth(depth) || !isOpenCvTypeLayout(layout))
    {
        return std::nullopt;
    }
    if (channels < 1 || channels > layout.maxChannels)
    {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(depth) + ((channels - 1) << layout.channelShift);
}

/// The depth held in a packed type code, under the caller's own packing.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param typeCode the code, as `cv::Mat::type()` reports it.
/// @param layout the packing the caller's OpenCV uses.
/// @return the depth, or an empty optional when the packing is unusable or the depth is one this
///         engine does not map.
[[nodiscard]] constexpr std::optional<OpenCvDepth> openCvDepthOfTypeCode(std::int32_t typeCode,
                                                                         const OpenCvTypeLayout& layout) noexcept
{
    if (!isOpenCvTypeLayout(layout) || typeCode < 0)
    {
        return std::nullopt;
    }
    const auto depth = static_cast<OpenCvDepth>(typeCode & layout.depthMask);
    return isOpenCvDepth(depth) ? std::optional<OpenCvDepth>{depth} : std::nullopt;
}

/// The channel count held in a packed type code, under the caller's own packing.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param typeCode the code, as `cv::Mat::type()` reports it.
/// @param layout the packing the caller's OpenCV uses.
/// @return the channel count, or an empty optional when the packing is unusable, the code is
///         negative, or the count is outside what a matrix may have.
[[nodiscard]] constexpr std::optional<std::int32_t> openCvChannelsOfTypeCode(std::int32_t typeCode,
                                                                             const OpenCvTypeLayout& layout) noexcept
{
    if (!isOpenCvTypeLayout(layout) || typeCode < 0)
    {
        return std::nullopt;
    }
    const std::int32_t channels = (typeCode >> layout.channelShift) + 1;
    return channels >= 1 && channels <= layout.maxChannels ? std::optional<std::int32_t>{channels} : std::nullopt;
}

/// Whether the caller's OpenCV packs type codes the way these functions compute them.
///
/// The one call that keeps a hand-written ABI honest. A consumer that has the OpenCV headers fills
/// an `OpenCvTypeLayout` from `CV_CN_SHIFT`, `CV_MAT_DEPTH_MASK` and `CV_CN_MAX`, passes it here
/// alongside the depth constants it can see, and gets a refusal rather than a wrong matrix if this
/// engine's table has drifted from the library's.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param layout the packing the caller's OpenCV uses.
/// @param unsigned8 the caller's `CV_8U`.
/// @param signed8 the caller's `CV_8S`.
/// @param unsigned16 the caller's `CV_16U`.
/// @param signed16 the caller's `CV_16S`.
/// @param signed32 the caller's `CV_32S`.
/// @param float32 the caller's `CV_32F`.
/// @param float64 the caller's `CV_64F`.
/// @return `BridgeRefusal::None` when every constant matches this engine's table and the packing is
///         usable; `BridgeRefusal::ArrangementNotRepresentable` when the packing itself is unusable,
///         which is checked first; or `BridgeRefusal::SampleTypeNotRepresentable` when a depth
///         constant differs.
[[nodiscard]] constexpr BridgeRefusal checkOpenCvTypeLayout(const OpenCvTypeLayout& layout, std::int32_t unsigned8,
                                                            std::int32_t signed8, std::int32_t unsigned16,
                                                            std::int32_t signed16, std::int32_t signed32,
                                                            std::int32_t float32, std::int32_t float64) noexcept
{
    if (!isOpenCvTypeLayout(layout))
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (unsigned8 != static_cast<std::int32_t>(OpenCvDepth::Unsigned8) ||
        signed8 != static_cast<std::int32_t>(OpenCvDepth::Signed8) ||
        unsigned16 != static_cast<std::int32_t>(OpenCvDepth::Unsigned16) ||
        signed16 != static_cast<std::int32_t>(OpenCvDepth::Signed16) ||
        signed32 != static_cast<std::int32_t>(OpenCvDepth::Signed32) ||
        float32 != static_cast<std::int32_t>(OpenCvDepth::Float32) ||
        float64 != static_cast<std::int32_t>(OpenCvDepth::Float64))
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    return BridgeRefusal::None;
}

/// Whether one plane's samples can be shared with OpenCV as they stand.
///
/// The layout half of the bridge, taken apart from the contract half so that a caller reading a
/// refusal knows which of the two refused. What it comes to: a matrix indexes with `int` and steps
/// in bytes, its samples of one pixel are adjacent, and its base pointer is whatever the caller
/// hands it - so a plane whose row stride and dimensions fit those types is shareable, whatever
/// padding it has, and a plane whose samples of one pixel are apart is not.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type what one sample is.
/// @param layout where the samples sit.
/// @param channelsInPixel how many samples of this plane belong to one pixel; the interleaved count
///        a matrix would carry, which is `ImageLayout::samplesPerPixel`.
/// @param typeLayout the packing the caller's OpenCV uses.
/// @return `BridgeRefusal::None` when a matrix can be built over those samples; otherwise the first
///         rule that refused: `ArrangementNotRepresentable` for an unusable packing,
///         `SampleTypeNotRepresentable` for `SampleType::UInt32` or a layout whose element size is
///         not the sample's, `ChannelCountNotRepresentable` for a channel count of zero or above
///         the packing's maximum, `ArrangementNotRepresentable` when the layout's samples per pixel
///         are not that count, `ExtentExceedsLibraryIndex` for a dimension beyond `int`, or
///         `RowStrideNotRepresentable` for a stride beyond `size_t`.
[[nodiscard]] constexpr BridgeRefusal checkOpenCvGeometry(SampleType type, const ImageLayout& layout,
                                                          std::uint32_t channelsInPixel,
                                                          const OpenCvTypeLayout& typeLayout) noexcept
{
    if (!isOpenCvTypeLayout(typeLayout))
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (!openCvDepthOf(type).has_value())
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(type))
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    if (channelsInPixel < 1u || channelsInPixel > static_cast<std::uint32_t>(typeLayout.maxChannels))
    {
        return BridgeRefusal::ChannelCountNotRepresentable;
    }
    if (layout.samplesPerPixel() != channelsInPixel)
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    constexpr auto kMaxIndex = static_cast<std::uint64_t>(0x7fffffff);
    if (layout.extent().width() > kMaxIndex || layout.extent().height() > kMaxIndex)
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }
    if (layout.rowStrideBytes() > static_cast<std::uint64_t>(static_cast<std::size_t>(-1)))
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    return BridgeRefusal::None;
}

/// Whether this plane can be handed to OpenCV under this contract, without copying.
///
/// The whole bridge: the claim and then the layout, in that order, so a refusal about what the
/// samples mean is never hidden behind one about how they sit.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT the plane's sample type; a const one describes read-only samples.
/// @param plane the samples to share.
/// @param contract what the bridge is being told about them.
/// @param channelsInPixel how many samples of this plane belong to one pixel.
/// @param typeLayout the packing the caller's OpenCV uses.
/// @return `BridgeRefusal::None` when the samples can be shared; otherwise the first rule that
///         refused.
template <ViewableSample SampleT>
[[nodiscard]] constexpr BridgeRefusal
checkOpenCvMat(const PlaneView<SampleT>& plane, const ExternalBufferContract& contract, std::uint32_t channelsInPixel,
               const OpenCvTypeLayout& typeLayout) noexcept
{
    const BridgeRefusal claim = checkExternalBufferContract(contract, !std::is_const_v<SampleT>);
    if (claim != BridgeRefusal::None)
    {
        return claim;
    }
    if (plane.data() == nullptr)
    {
        return BridgeRefusal::NoSamples;
    }
    return checkOpenCvGeometry(PlaneView<SampleT>::kSampleType, plane.layout(), channelsInPixel, typeLayout);
}

/// The matrix description for one plane, when the bridge holds.
///
/// The caller builds `cv::Mat(description.rows, description.cols, description.type,
/// description.data, description.stepBytes)` from what comes back, and every argument of that
/// constructor has been checked. When the answer is empty, `checkOpenCvMat` names the rule.
///
/// The description borrows: it points at the plane's samples and owns nothing, so it must not
/// outlive them, which is what `BorrowScope` in the contract is for.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT the plane's sample type; a const one describes read-only samples.
/// @param plane the samples to share.
/// @param contract what the bridge is being told about them.
/// @param channelsInPixel how many samples of this plane belong to one pixel.
/// @param typeLayout the packing the caller's OpenCV uses.
/// @return the description, or an empty optional when any rule refuses.
template <ViewableSample SampleT>
[[nodiscard]] constexpr std::optional<OpenCvMatDescription>
describeAsOpenCvMat(const PlaneView<SampleT>& plane, const ExternalBufferContract& contract,
                    std::uint32_t channelsInPixel, const OpenCvTypeLayout& typeLayout) noexcept
{
    if (checkOpenCvMat(plane, contract, channelsInPixel, typeLayout) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    const std::optional<OpenCvDepth> depth = openCvDepthOf(PlaneView<SampleT>::kSampleType);
    const std::optional<std::int32_t> code =
        openCvTypeCode(*depth, static_cast<std::int32_t>(channelsInPixel), typeLayout);
    if (!code.has_value())
    {
        return std::nullopt;
    }
    return OpenCvMatDescription{.data = const_cast<std::remove_const_t<SampleT>*>(plane.data()),
                                .rows = static_cast<std::int32_t>(plane.height()),
                                .cols = static_cast<std::int32_t>(plane.width()),
                                .type = *code,
                                .stepBytes = static_cast<std::size_t>(plane.rowStrideBytes())};
}

/// Whether a matrix somebody else owns can be viewed by this engine as it stands.
///
/// The reverse direction, and the one where ownership matters most: a matrix's samples belong to
/// OpenCV's reference-counted allocation, so a view over them is a borrow whatever the engine does
/// with it, and adopting them into owned storage is never zero-copy. A matrix is always writable
/// through its own pointer, so the contract's mutability never refuses here.
///
/// Thread-safe and allocation-free.
///
/// @param description the matrix, as its own fields report it.
/// @param contract what the bridge is being told about it.
/// @param typeLayout the packing the caller's OpenCV uses.
/// @return `BridgeRefusal::None` when a view can be built over it; otherwise the first rule that
///         refused: whatever `checkExternalBufferContract` reports, `ArrangementNotRepresentable`
///         for an unusable packing, `NoSamples` for a null base, `ExtentExceedsLibraryIndex` for a
///         dimension that is not positive, `SampleTypeNotRepresentable` for a depth this engine
///         does not map, `ChannelCountNotRepresentable` for a channel count the packing cannot
///         hold, or `RowStrideNotRepresentable` for a step that is not a whole number of samples or
///         is shorter than a row.
[[nodiscard]] AFTERNIGHTENGINE_API BridgeRefusal adoptOpenCvMatCheck(const OpenCvMatDescription& description,
                                                                     const ExternalBufferContract& contract,
                                                                     const OpenCvTypeLayout& typeLayout) noexcept;

/// The engine layout a matrix's samples already have.
///
/// What makes the copy path explicit rather than implied. A matrix this engine cannot share - a
/// three-channel one, most often - is still describable as an engine layout, so the caller builds a
/// view over OpenCV's memory and moves the samples with `Image/ImageConversion.h`, where the
/// decisions a conversion makes are stated and the cost is visible.
///
/// The layout describes one interleaved plane: `samplesPerPixel` carries the matrix's channel count
/// and `planeCount` is one, which is what a matrix's memory actually is.
///
/// Thread-safe and allocation-free.
///
/// @param description the matrix, as its own fields report it.
/// @param typeLayout the packing the caller's OpenCV uses.
/// @param limits the geometry limits in force, which the resulting extent is checked against.
/// @return the layout, or an empty optional when the matrix is not describable - an unmapped depth,
///         a step that is not a whole number of samples, an extent outside the limits.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageLayout> openCvMatLayout(const OpenCvMatDescription& description,
                                                                              const OpenCvTypeLayout& typeLayout,
                                                                              const EngineLimits& limits) noexcept;

/// The engine representation a matrix's samples already are.
///
/// Separate from the layout because a caller needs it to choose the view's template argument before
/// it has a layout to build one with.
///
/// Thread-safe and allocation-free.
///
/// @param description the matrix, as its own fields report it.
/// @param typeLayout the packing the caller's OpenCV uses.
/// @return the representation, or an empty optional when the depth is one this engine does not map.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<SampleType>
openCvMatSampleType(const OpenCvMatDescription& description, const OpenCvTypeLayout& typeLayout) noexcept;

} // namespace an
