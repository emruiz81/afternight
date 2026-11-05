// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Describing engine samples as a Python buffer, and Python's as engine ones.
///
/// Python's buffer protocol is a base pointer, an item size, a `struct`-module format code, a number
/// of axes, and a shape and a stride per axis. Every one of those is a fact this adapter can check
/// before anything reaches the interpreter, which is the point: the failure a bridge produces is not
/// a compile error but a NumPy array that quietly describes the wrong pixels, or an engine image that
/// quietly adopts somebody else's memory.
///
/// No Python header is included here and no Python type appears in this surface. The description
/// below carries the fields `Py_buffer` carries, in the types it uses, so a caller with an
/// interpreter builds a memoryview from a checked description and a caller without one pays nothing.
///
/// The interesting difference from the engine's other bridges is that Python's model is strided on
/// every axis, so a *planar* image shares without copying - which `cv::Mat` cannot do, because its
/// channels interleave within a row. What this bridge refuses is therefore a shorter list, and the
/// refusals that remain are real:
///
///   - an arrangement needing four axes. A semi-planar image - several planes, each interleaving
///     several of a pixel's samples - cannot be described in the three axes this bridge uses, and
///     flattening one would silently conflate a channel with a column;
///   - a stride on the fastest axis that is not exactly one item. An array sliced or transposed on
///     its last axis is a valid buffer in Python's terms and is not a row of samples in this
///     engine's terms; and
///   - a format code this engine has no representation for. Python's format alphabet is much larger
///     than eight resident types: `?`, `e`, `q`, `Zf` and a foreign byte order all name real buffers
///     this engine cannot describe, so they are refused by name rather than reinterpreted.
///
/// The contract half of every bridge is the engine's own `Interop/ExternalBuffer.h` vocabulary, used
/// unchanged. An adapter outside the engine binary shares the refusal alphabet rather than inventing
/// a parallel one, so a caller that already branches on `BridgeRefusal` for OpenCV branches on the
/// same values here.

#pragma once

#include <AfterNightEnginePythonAdapter/Api.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace an
{

/// The `struct`-module format codes this engine has a representation for.
///
/// An enumeration rather than a character, so a format cannot be passed where an item size belongs,
/// and a closed one, so a buffer whose code is outside it is refused rather than guessed at. The
/// spelling of each code is native byte order and no alignment character, which is what a buffer
/// this engine can share must be.
enum class PythonBufferFormat : std::uint8_t
{
    /// `B`: unsigned 8-bit.
    UInt8,

    /// `b`: signed 8-bit.
    Int8,

    /// `H`: unsigned 16-bit.
    UInt16,

    /// `h`: signed 16-bit.
    Int16,

    /// `I`: unsigned 32-bit. Python's `I` is C's `unsigned int`, which is 32 bits on every platform
    /// this engine builds for; `L` is not used, because its width follows the platform's `long`.
    UInt32,

    /// `i`: signed 32-bit, for the reason `I` is not `L`.
    Int32,

    /// `f`: single-precision floating point.
    Float32,

    /// `d`: double-precision floating point.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Float64
};

/// How many axes a buffer this bridge describes may have.
///
/// Three, because that is what the engine's two shareable arrangements need: plane, row and column
/// for planar storage, and row, column and sample-in-pixel for interleaved storage. A fourth axis
/// would be a semi-planar image, which is refused by name.
inline constexpr std::int32_t kMaxPythonBufferAxes = 3;

/// One `Py_buffer`'s worth of description: everything needed to build a memoryview over samples
/// somebody else owns, and nothing Python would compute for itself.
///
/// The field types are the ones `Py_buffer` uses - a `void*` base, `Py_ssize_t` lengths and strides -
/// so a caller assigns them across without a cast that could truncate. `Py_ssize_t` is spelled as
/// `std::int64_t` here because this header includes no Python header; the two are the same type on
/// every platform this engine builds for, and `adoptPythonBufferCheck` refuses a value that does not
/// fit an engine layout anyway.
///
/// The base pointer is `void*` and its constness lives in the contract that produced the description
/// and in `readOnly`, exactly as it does in `Py_buffer`.
struct PythonBufferDescription
{
    /// `Py_buffer::buf`: the first sample. Never null in a description this adapter produced.
    void* data;

    /// `Py_buffer::len`: the logical size in bytes, which the protocol defines as the product of the
    /// shape times the item size. It is not the addressed extent: a padded buffer spans more memory
    /// than this, and `pythonBufferLayout` is what reports the layout that describes it.
    std::int64_t lengthBytes;

    /// `Py_buffer::itemsize`: one sample's size in bytes, which must agree with `format`.
    std::int64_t itemSizeBytes;

    /// `Py_buffer::ndim`: how many axes carry meaning, which is 2 or 3 here.
    std::int32_t axes;

    /// `Py_buffer::shape`: how many items each axis holds, slowest axis first. Entries beyond
    /// `axes` are zero.
    std::array<std::int64_t, kMaxPythonBufferAxes> shape;

    /// `Py_buffer::strides`: the byte distance between consecutive items of each axis, slowest axis
    /// first. Entries beyond `axes` are zero.
    std::array<std::int64_t, kMaxPythonBufferAxes> strides;

    /// `Py_buffer::format`: what one item is, as one of the codes this engine represents.
    PythonBufferFormat format;

    /// `Py_buffer::readonly`: whether the borrower may only read. A description built for a
    /// read-only contract is read-only, and a description of somebody else's read-only buffer
    /// cannot be written through.
    bool readOnly;

    /// Whether two descriptions describe the same buffer.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when every field is equal, the unused shape and stride entries included.
    [[nodiscard]] friend constexpr bool operator==(const PythonBufferDescription& left,
                                                   const PythonBufferDescription& right) noexcept = default;
};

/// Whether a value is one of the formats this engine represents.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isPythonBufferFormat(PythonBufferFormat format) noexcept
{
    return static_cast<std::uint8_t>(format) <= static_cast<std::uint8_t>(PythonBufferFormat::Float64);
}

/// The format's own code, as a `struct`-module string.
///
/// Native byte order and no alignment character, which is what the protocol calls the "native"
/// spelling and what a shared buffer must be.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the format to spell. A value outside the enumeration spells the empty string, which
///        no buffer accepts.
/// @return a null-terminated string literal, which outlives every caller and is never freed.
[[nodiscard]] constexpr const char* pythonBufferFormatCode(PythonBufferFormat format) noexcept
{
    switch (format)
    {
    case PythonBufferFormat::UInt8:
        return "B";
    case PythonBufferFormat::Int8:
        return "b";
    case PythonBufferFormat::UInt16:
        return "H";
    case PythonBufferFormat::Int16:
        return "h";
    case PythonBufferFormat::UInt32:
        return "I";
    case PythonBufferFormat::Int32:
        return "i";
    case PythonBufferFormat::Float32:
        return "f";
    case PythonBufferFormat::Float64:
        return "d";
    }
    return "";
}

/// The format that means the same thing as an engine sample type.
///
/// Total, unlike the OpenCV table: every one of the eight resident representations has a format
/// code, because Python's alphabet covers all of them. The reverse direction is the partial one.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the engine representation.
/// @return the format, or an empty optional when `type` is outside the enumeration.
[[nodiscard]] constexpr std::optional<PythonBufferFormat> pythonBufferFormatOf(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::UInt8:
        return PythonBufferFormat::UInt8;
    case SampleType::Int8:
        return PythonBufferFormat::Int8;
    case SampleType::UInt16:
        return PythonBufferFormat::UInt16;
    case SampleType::Int16:
        return PythonBufferFormat::Int16;
    case SampleType::UInt32:
        return PythonBufferFormat::UInt32;
    case SampleType::Int32:
        return PythonBufferFormat::Int32;
    case SampleType::Float32:
        return PythonBufferFormat::Float32;
    case SampleType::Float64:
        return PythonBufferFormat::Float64;
    }
    return std::nullopt;
}

/// The engine representation a format means.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the format code.
/// @return the representation, or an empty optional when `format` is outside the enumeration.
[[nodiscard]] constexpr std::optional<SampleType> pythonSampleTypeOf(PythonBufferFormat format) noexcept
{
    switch (format)
    {
    case PythonBufferFormat::UInt8:
        return SampleType::UInt8;
    case PythonBufferFormat::Int8:
        return SampleType::Int8;
    case PythonBufferFormat::UInt16:
        return SampleType::UInt16;
    case PythonBufferFormat::Int16:
        return SampleType::Int16;
    case PythonBufferFormat::UInt32:
        return SampleType::UInt32;
    case PythonBufferFormat::Int32:
        return SampleType::Int32;
    case PythonBufferFormat::Float32:
        return SampleType::Float32;
    case PythonBufferFormat::Float64:
        return SampleType::Float64;
    }
    return std::nullopt;
}

/// The format a `struct`-module code string names, if this engine represents it.
///
/// Deliberately strict. A code carrying a byte-order or alignment character - `<f`, `>H`, `=i`, `!d`,
/// `@B` - is refused rather than accepted on the platforms where it happens to agree with the
/// native order, because a buffer that states its order is a buffer that may one day state the other
/// one. A code this engine has no representation for - `?`, `e`, `q`, `n`, `Zf`, `s` - is refused for
/// the reason it exists: there is no resident type to describe it with.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param code the format string, as `Py_buffer::format` carries it. A null or empty code names
///        nothing; the protocol's own "unsigned bytes" default is not assumed here, because a buffer
///        this adapter shares always states its format.
/// @return the format, or an empty optional when the code is not one of the eight native spellings.
[[nodiscard]] constexpr std::optional<PythonBufferFormat> pythonBufferFormatOfCode(std::string_view code) noexcept
{
    if (code.size() != 1u)
    {
        return std::nullopt;
    }
    switch (code.front())
    {
    case 'B':
        return PythonBufferFormat::UInt8;
    case 'b':
        return PythonBufferFormat::Int8;
    case 'H':
        return PythonBufferFormat::UInt16;
    case 'h':
        return PythonBufferFormat::Int16;
    case 'I':
        return PythonBufferFormat::UInt32;
    case 'i':
        return PythonBufferFormat::Int32;
    case 'f':
        return PythonBufferFormat::Float32;
    case 'd':
        return PythonBufferFormat::Float64;
    default:
        return std::nullopt;
    }
}

/// Whether an image layout can be described to Python at all, before any claim is looked at.
///
/// The layout half of the bridge, and the half that decides zero-copy. The order is a contract:
/// membership of the closed sets first, then the representation, then the arrangement, then the
/// indices, so a layout with two problems reports the same one on every build.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type what one sample is.
/// @param layout where the samples sit.
/// @return `BridgeRefusal::None` when a buffer can be described over those samples; otherwise the
///         first rule that refused. `UnknownContract` for a type outside the enumeration;
///         `SampleTypeNotRepresentable` for an element size that is not the type's;
///         `ArrangementNotRepresentable` for semi-planar storage; `ExtentExceedsLibraryIndex` for a
///         total size, and `RowStrideNotRepresentable` for a row or plane stride, beyond what a
///         signed 64-bit index holds.
[[nodiscard]] constexpr BridgeRefusal checkPythonBufferGeometry(SampleType type, const ImageLayout& layout) noexcept
{
    if (!isSampleType(type))
    {
        return BridgeRefusal::UnknownContract;
    }
    if (!pythonBufferFormatOf(type).has_value() || layout.elementSizeBytes() != sampleTypeSizeBytes(type))
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    if (storageArrangementOf(layout) == StorageArrangement::SemiPlanar)
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    constexpr auto kMaxIndex = static_cast<std::uint64_t>(0x7fffffffffffffff);
    if (layout.totalBytes() > kMaxIndex)
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }
    if (layout.rowStrideBytes() > kMaxIndex || layout.planeStrideBytes() > kMaxIndex)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    return BridgeRefusal::None;
}

/// Whether this image can be handed to Python under this contract, without copying.
///
/// The whole bridge: the claim and then the layout, in that order, so a refusal about what the
/// samples mean is never hidden behind one about how they sit.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT the image's sample type; a const one describes read-only samples.
/// @param image the samples to share.
/// @param contract what the bridge is being told about them.
/// @return `BridgeRefusal::None` when the samples can be shared; otherwise the first rule that
///         refused.
template <ViewableSample SampleT>
[[nodiscard]] constexpr BridgeRefusal checkPythonBuffer(const ImageView<SampleT>& image,
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
    return checkPythonBufferGeometry(ImageView<SampleT>::kSampleType, image.layout());
}

/// The buffer description for one image, when the bridge holds.
///
/// The caller fills a `Py_buffer` from what comes back and builds a memoryview from it, and every
/// field of that structure has been checked. When the answer is empty, `checkPythonBuffer` names the
/// rule.
///
/// The axes are the arrangement stated rather than a convention to remember. Planar storage is
/// described as `(plane, row, column)` and a single-plane planar image as `(row, column)`, so a
/// monochrome image reaches Python as a two-dimensional array and an RGB one as three planes.
/// Interleaved storage is described as `(row, column, sample)`, so a pixel's samples are the fastest
/// axis, which is what they are in memory.
///
/// The description borrows: it points at the image's samples and owns nothing, so it must not
/// outlive them, which is what `BorrowScope` in the contract is for.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT the image's sample type; a const one describes read-only samples.
/// @param image the samples to share.
/// @param contract what the bridge is being told about them.
/// @return the description, or an empty optional when any rule refuses.
template <ViewableSample SampleT>
[[nodiscard]] constexpr std::optional<PythonBufferDescription>
describeAsPythonBuffer(const ImageView<SampleT>& image, const ExternalBufferContract& contract) noexcept
{
    if (checkPythonBuffer(image, contract) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    const std::optional<PythonBufferFormat> format = pythonBufferFormatOf(ImageView<SampleT>::kSampleType);
    if (!format.has_value())
    {
        return std::nullopt;
    }
    const ImageLayout& layout = image.layout();
    const auto itemSize = static_cast<std::int64_t>(layout.elementSizeBytes());
    const auto width = static_cast<std::int64_t>(layout.extent().width());
    const auto height = static_cast<std::int64_t>(layout.extent().height());
    const auto samplesPerPixel = static_cast<std::int64_t>(layout.samplesPerPixel());
    const auto planes = static_cast<std::int64_t>(layout.planeCount());
    const auto rowStride = static_cast<std::int64_t>(layout.rowStrideBytes());
    const auto planeStride = static_cast<std::int64_t>(layout.planeStrideBytes());

    PythonBufferDescription description{};
    description.data = const_cast<std::remove_const_t<SampleT>*>(image.data());
    description.itemSizeBytes = itemSize;
    description.format = *format;
    description.readOnly = std::is_const_v<SampleT> || contract.mutability == BufferMutability::ReadOnly;
    if (samplesPerPixel > 1)
    {
        description.axes = 3;
        description.shape = {height, width, samplesPerPixel};
        description.strides = {rowStride, samplesPerPixel * itemSize, itemSize};
        description.lengthBytes = height * width * samplesPerPixel * itemSize;
        return description;
    }
    if (planes > 1)
    {
        description.axes = 3;
        description.shape = {planes, height, width};
        description.strides = {planeStride, rowStride, itemSize};
        description.lengthBytes = planes * height * width * itemSize;
        return description;
    }
    description.axes = 2;
    description.shape = {height, width, 0};
    description.strides = {rowStride, itemSize, 0};
    description.lengthBytes = height * width * itemSize;
    return description;
}

/// Whether a buffer somebody else owns can be viewed by this engine as it stands.
///
/// The reverse direction. A Python object's samples belong to that object's own allocation, so a
/// view over them is a borrow whatever the engine does with it, and adopting them into owned storage
/// is never zero-copy.
///
/// The arrangement is stated by the caller because a three-axis buffer does not say which reading it
/// is. `(2, 3, 4)` with strides `(48, 16, 4)` is two planes of three rows of four samples, and it is
/// also three rows of four-sample pixels - the numbers are identical and the images are different.
/// This engine will not guess between them, so a caller that received a buffer says what it asked
/// for, and the answer is checked against that.
///
/// Thread-safe and allocation-free.
///
/// @param description the buffer, as its own fields report it.
/// @param contract what the bridge is being told about it.
/// @param arrangement which reading of a three-axis buffer the caller means. A two-axis buffer is
///        one plane of one sample per pixel whichever value is passed, because those are the same
///        bytes. `StorageArrangement::SemiPlanar` is refused: it needs a fourth axis.
/// @return `BridgeRefusal::None` when a view can be built over it; otherwise the first rule that
///         refused, in this order: the contract's own refusal; `NoSamples` for a null base;
///         `UnknownContract` for a format or arrangement outside its enumeration;
///         `ArrangementNotRepresentable` for a semi-planar arrangement;
///         `SampleTypeNotRepresentable` for an item size that is not the format's;
///         `ArrangementNotRepresentable` for an axis count other than two or three or a fastest-axis
///         stride that is not one item; `ExtentExceedsLibraryIndex` for an extent that is not
///         positive or does not fit an engine extent; `ArrangementNotRepresentable` for interleaved
///         pixels with a gap between them; and `RowStrideNotRepresentable` for a row or plane stride
///         that is not positive or makes rows or planes overlap.
[[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API BridgeRefusal
adoptPythonBufferCheck(const PythonBufferDescription& description, const ExternalBufferContract& contract,
                       StorageArrangement arrangement) noexcept;

/// The engine layout a Python buffer's samples already have.
///
/// What makes the copy path explicit rather than implied. The buffer's own fields are judged by the
/// rules `adoptPythonBufferCheck` applies after the contract, and when they hold the layout keeps the
/// buffer's strides and padding exactly. When they do not - most often a strided fastest axis, which
/// is what a sliced or transposed array is - the answer is empty and the caller converts through
/// the engine's own conversion instead of this adapter reinterpreting anything.
///
/// Thread-safe and allocation-free.
///
/// @param description the buffer, as its own fields report it.
/// @param arrangement which reading of a three-axis buffer the caller means, on the same terms as
///        `adoptPythonBufferCheck`.
/// @param limits the geometry limits in force, which the resulting extent is checked against.
/// @return the layout, or an empty optional when the buffer is not describable as one - a strided
///         fastest axis, an axis count outside two or three, an extent outside the limits.
[[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API std::optional<ImageLayout>
pythonBufferLayout(const PythonBufferDescription& description, StorageArrangement arrangement,
                   const EngineLimits& limits) noexcept;

/// The engine representation a Python buffer's samples already are.
///
/// Separate from the layout because a caller needs it to choose a view's template argument before it
/// has a layout to build one with.
///
/// Thread-safe and allocation-free.
///
/// @param description the buffer, as its own fields report it.
/// @return the representation, or an empty optional when the format is one this engine does not
///         represent.
[[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API std::optional<SampleType>
pythonBufferSampleType(const PythonBufferDescription& description) noexcept;

} // namespace an
