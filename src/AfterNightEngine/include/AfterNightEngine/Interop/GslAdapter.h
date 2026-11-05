// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Describing engine samples as a GSL matrix or vector, and GSL's as engine ones.
///
/// A `gsl_matrix` is five fields: two sizes, a trailing dimension, a data pointer, a block pointer,
/// and an owner flag. Four of them this engine can fill from a plane; the fifth is the one that
/// matters most. `owner` tells `gsl_matrix_free` whether to release the block, so a matrix built
/// over engine memory with `owner` set is a `free()` of memory the engine still holds, and a matrix
/// built over engine memory with `block` set is the same thing one indirection along. This adapter
/// produces descriptions in which both are absent, which is what `gsl_matrix_view_array_with_tda`
/// produces too - and it says so rather than leaving it to be noticed.
///
/// The trailing dimension is GSL's row stride and it is counted in *elements*, so a plane whose row
/// stride is not a whole number of elements cannot be described at all. GSL's sizes are `size_t`, so
/// nothing here refuses an engine extent for being too large; what it refuses is a stride that is not
/// a whole number of elements, a sample type GSL has no matrix for, and a plane whose pixels hold
/// more than one sample - because a `gsl_matrix` element is one number and there is no interleaved
/// form. The reverse direction does refuse a GSL dimension beyond the engine's own 32-bit extent.
///
/// No GSL header is included. What this header offers is the checked description a caller builds
/// its `gsl_matrix` and `gsl_vector` views from; the one place the engine calls GSL itself is its
/// GSL adapter directory, for a Student t quantile.

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

/// Which of GSL's floating-point matrix families a description belongs to.
///
/// The suffix on every entry point - `gsl_matrix_float_` against `gsl_matrix_` - and therefore a
/// property of the matrix rather than a detail of it. GSL has integer families too; this engine
/// bridges the floating ones, because the operations it reaches GSL for are fits and solves.
enum class GslPrecision : std::uint8_t
{
    /// `gsl_matrix_float` and `gsl_vector_float`: elements are `float`.
    Single,

    /// `gsl_matrix` and `gsl_vector`: elements are `double`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Double
};

/// One `gsl_matrix` header's worth of description: a row-major block with an element stride.
///
/// The field names are GSL's own, so a caller assigning them across has nothing to translate. The
/// two fields that are not here are the point of the type: `block` and `owner` are always absent
/// from a matrix this engine describes, because both would give GSL a claim on memory it did not
/// allocate.
struct GslMatrixDescription
{
    /// The first element. Never null in a description this engine produced. A caller with the GSL
    /// headers reinterprets it as `double*` or `float*` according to `precision`.
    void* data;

    /// Which precision the elements are.
    GslPrecision precision;

    /// `gsl_matrix::size1`: the number of rows.
    std::size_t size1;

    /// `gsl_matrix::size2`: the number of columns.
    std::size_t size2;

    /// `gsl_matrix::tda`: the trailing dimension, which is the row stride counted in elements. At
    /// least `size2`; larger when the plane it describes has padding.
    std::size_t tda;

    /// Whether two descriptions describe the same matrix.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when all five fields are equal.
    [[nodiscard]] friend constexpr bool operator==(const GslMatrixDescription& left,
                                                   const GslMatrixDescription& right) noexcept = default;
};

/// One `gsl_vector` header's worth of description: a strided run of elements.
///
/// The form a single row, a single column, or a whole contiguous plane takes when GSL is asked to
/// treat it as one sequence. `stride` is in elements, as GSL counts it.
struct GslVectorDescription
{
    /// The first element. Never null in a description this engine produced.
    void* data;

    /// Which precision the elements are.
    GslPrecision precision;

    /// `gsl_vector::size`: how many elements the vector holds.
    std::size_t size;

    /// `gsl_vector::stride`: the distance between consecutive elements, counted in elements. One
    /// for a contiguous run.
    std::size_t stride;

    /// Whether two descriptions describe the same vector.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when all four fields are equal.
    [[nodiscard]] friend constexpr bool operator==(const GslVectorDescription& left,
                                                   const GslVectorDescription& right) noexcept = default;
};

/// Whether a value is one of the precisions.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param precision the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isGslPrecision(GslPrecision precision) noexcept
{
    return static_cast<std::uint8_t>(precision) <= static_cast<std::uint8_t>(GslPrecision::Double);
}

/// The GSL family an engine sample type belongs to, when there is one.
///
/// A closed table with six gaps, for the same reason FFTW's has six: this engine bridges GSL's
/// floating families, and an integer image is converted before it is fitted rather than reinterpreted
/// as a matrix of a family whose arithmetic is different.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the engine representation.
/// @return the family, or an empty optional when this engine does not bridge one for it.
[[nodiscard]] constexpr std::optional<GslPrecision> gslPrecisionOf(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::Float32:
        return GslPrecision::Single;
    case SampleType::Float64:
        return GslPrecision::Double;
    case SampleType::UInt8:
    case SampleType::Int8:
    case SampleType::UInt16:
    case SampleType::Int16:
    case SampleType::UInt32:
    case SampleType::Int32:
        return std::nullopt;
    }
    return std::nullopt;
}

/// The engine representation one element of a GSL family is.
///
/// The other direction of the same table.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param precision the GSL family.
/// @return the representation, or an empty optional for a value outside the enumeration.
[[nodiscard]] constexpr std::optional<SampleType> gslSampleTypeOf(GslPrecision precision) noexcept
{
    switch (precision)
    {
    case GslPrecision::Single:
        return SampleType::Float32;
    case GslPrecision::Double:
        return SampleType::Float64;
    }
    return std::nullopt;
}

/// Whether one plane's samples can be described as a GSL matrix as they stand.
///
/// The layout half of the bridge. GSL counts its trailing dimension in elements, so the plane's row
/// stride has to be a whole number of them, and a matrix element is one number, so the plane has to
/// hold one sample per pixel. GSL's constructors refuse a zero-sized matrix, which never arises here
/// because no layout has a zero dimension.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type what one sample is.
/// @param layout where the samples sit.
/// @return `BridgeRefusal::None` when a matrix can be built over those samples; otherwise the first
///         rule that refused: `SampleTypeNotRepresentable` for an integer representation or a
///         layout whose element size is not the sample's, `ArrangementNotRepresentable` for more
///         than one sample per pixel, or `RowStrideNotRepresentable` for a stride that is not a
///         whole number of elements or does not fit `size_t`.
[[nodiscard]] constexpr BridgeRefusal checkGslGeometry(SampleType type, const ImageLayout& layout) noexcept
{
    if (!gslPrecisionOf(type).has_value())
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(type))
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    if (layout.samplesPerPixel() != 1u)
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    const auto elementSize = static_cast<std::uint64_t>(layout.elementSizeBytes());
    if (layout.rowStrideBytes() % elementSize != 0u)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    constexpr auto kMaxSize = static_cast<std::uint64_t>(static_cast<std::size_t>(-1));
    if (layout.rowStrideBytes() / elementSize > kMaxSize)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    return BridgeRefusal::None;
}

/// Whether this plane can be handed to GSL under this contract, without copying.
///
/// The whole bridge: the claim, then the layout, in that order, so a refusal about what the samples
/// mean is never hidden behind one about how they sit.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT the plane's sample type; a const one describes read-only samples.
/// @param plane the samples to share.
/// @param contract what the bridge is being told about them.
/// @return `BridgeRefusal::None` when the samples can be shared; otherwise the first rule that
///         refused.
template <ViewableSample SampleT>
[[nodiscard]] constexpr BridgeRefusal checkGslMatrix(const PlaneView<SampleT>& plane,
                                                     const ExternalBufferContract& contract) noexcept
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
    return checkGslGeometry(PlaneView<SampleT>::kSampleType, plane.layout());
}

/// The matrix description for one plane, when the bridge holds.
///
/// The caller builds `gsl_matrix_view_array_with_tda(static_cast<double*>(description.data),
/// description.size1, description.size2, description.tda)` from what comes back - or its
/// `gsl_matrix_float_` counterpart over `float*` for single precision - which is the GSL
/// constructor that produces a matrix over memory it does not own, the only one that is safe over
/// engine samples.
///
/// The description borrows: it points at the plane's samples and owns nothing, so it must not
/// outlive them, which is what `BorrowScope` in the contract is for.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT the plane's sample type; a const one describes read-only samples.
/// @param plane the samples to share.
/// @param contract what the bridge is being told about them.
/// @return the description, or an empty optional when any rule refuses.
template <ViewableSample SampleT>
[[nodiscard]] constexpr std::optional<GslMatrixDescription>
describeAsGslMatrix(const PlaneView<SampleT>& plane, const ExternalBufferContract& contract) noexcept
{
    if (checkGslMatrix(plane, contract) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    const std::optional<GslPrecision> precision = gslPrecisionOf(PlaneView<SampleT>::kSampleType);
    if (!precision.has_value())
    {
        return std::nullopt;
    }
    return GslMatrixDescription{
        .data = const_cast<std::remove_const_t<SampleT>*>(plane.data()),
        .precision = *precision,
        .size1 = static_cast<std::size_t>(plane.height()),
        .size2 = static_cast<std::size_t>(plane.width()),
        .tda = static_cast<std::size_t>(plane.rowStrideBytes() / plane.layout().elementSizeBytes())};
}

/// The vector description for one row of a plane, when the bridge holds.
///
/// A row rather than a whole plane, because a plane with padding is not one run of elements and a
/// vector has one stride: describing the whole plane as a vector would either lie about the padding
/// or need a stride that skips within a row as well as between them.
///
/// Thread-safe and allocation-free.
///
/// @tparam SampleT the plane's sample type; a const one describes read-only samples.
/// @param plane the samples to share.
/// @param contract what the bridge is being told about them.
/// @param y which row, counted from the plane's own origin.
/// @return the description, or an empty optional when any rule refuses or `y` is outside the plane.
template <ViewableSample SampleT>
[[nodiscard]] inline std::optional<GslVectorDescription> describeRowAsGslVector(const PlaneView<SampleT>& plane,
                                                                                const ExternalBufferContract& contract,
                                                                                std::uint32_t y) noexcept
{
    if (checkGslMatrix(plane, contract) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    SampleT* const first = plane.sampleAt(0u, y, 0u);
    if (first == nullptr)
    {
        return std::nullopt;
    }
    const std::optional<GslPrecision> precision = gslPrecisionOf(PlaneView<SampleT>::kSampleType);
    if (!precision.has_value())
    {
        return std::nullopt;
    }
    return GslVectorDescription{.data = const_cast<std::remove_const_t<SampleT>*>(first),
                                .precision = *precision,
                                .size = static_cast<std::size_t>(plane.width()),
                                .stride = 1u};
}

/// Whether a matrix somebody else owns can be viewed by this engine as it stands.
///
/// The reverse direction. A GSL matrix's block belongs to GSL, so a view over it is a borrow
/// whatever the engine does with it. A GSL matrix is always writable through its own pointer, so the
/// contract's mutability never refuses here.
///
/// Thread-safe and allocation-free.
///
/// @param description the matrix, as its own fields report it.
/// @param contract what the bridge is being told about it.
/// @return `BridgeRefusal::None` when a view can be built over it; otherwise the first rule that
///         refused: whatever `checkExternalBufferContract` reports, `UnknownContract` for a
///         precision outside its enumeration, `NoSamples` for a null base,
///         `ExtentExceedsLibraryIndex` for a zero dimension or one beyond the engine's 32-bit
///         extent, or `RowStrideNotRepresentable` for a trailing dimension shorter than a row.
[[nodiscard]] AFTERNIGHTENGINE_API BridgeRefusal adoptGslMatrixCheck(const GslMatrixDescription& description,
                                                                     const ExternalBufferContract& contract) noexcept;

/// The engine layout a GSL matrix's elements already have.
///
/// What makes the copy path explicit rather than implied. A matrix this engine cannot share - one
/// of the wrong precision for the image - is still describable as an engine layout, so the caller
/// builds a view over GSL's memory and moves the samples with `Image/ImageConversion.h`, where the
/// decisions a conversion makes are stated and the cost is visible.
///
/// Thread-safe and allocation-free.
///
/// @param description the matrix, as its own fields report it.
/// @param limits the geometry limits in force, which the resulting extent is checked against.
/// @return the layout, or an empty optional when the matrix is not describable - a zero dimension,
///         a trailing dimension shorter than a row, an extent outside the limits.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageLayout> gslMatrixLayout(const GslMatrixDescription& description,
                                                                              const EngineLimits& limits) noexcept;

} // namespace an
