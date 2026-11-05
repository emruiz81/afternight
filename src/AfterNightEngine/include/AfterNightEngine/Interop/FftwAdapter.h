// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Describing engine samples as an FFTW array, and FFTW's as engine ones.
///
/// FFTW does not have an image type. A plan is built over a base pointer plus dimensions and strides
/// counted in *elements*, held in `int`, and an element is either one real number or two adjacent
/// ones. That makes three facts checkable before a plan exists, and each of them fails silently
/// otherwise: a row stride that is not a whole number of elements cannot be expressed at all, a
/// dimension that does not fit `int` wraps, and a base address that does not meet the alignment a
/// SIMD plan was created for produces either a crash or a plan that quietly refuses to run on the
/// buffer it was given.
///
/// The fourth fact is the one that costs people a morning. A real-to-complex transform computed in
/// place needs its real array padded along the last dimension - `2 * (width / 2 + 1)` reals per row
/// rather than `width` - because the complex output is written over the input and is one element per
/// two inputs, plus one. A real array packed tightly is the correct array for an out-of-place
/// transform and the wrong array for an in-place one, and the difference is invisible until the last
/// two columns of every row come back wrong. Here it is a stated `FftwPlacement` and a refusal by
/// name.
///
/// No FFTW header is included. `fftw_complex` is `double[2]` and `fftwf_complex` is `float[2]` - two
/// adjacent reals, which is a layout this engine can describe without naming the type - so a complex
/// array is described as its real element count and this engine's own `SampleType`, and the caller
/// reinterprets the base pointer at the one call site that has the FFTW headers. The engine's own
/// transforms are planned from these descriptions inside its FFTW adapter directory, the one place
/// the include rules let the library's header in.

#pragma once

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <cstdint>
#include <optional>
#include <type_traits>

namespace an
{

/// Which of FFTW's two precisions an array belongs to.
///
/// The prefix on every entry point - `fftw_` against `fftwf_` - and therefore a property of the
/// array rather than a detail of it: a double array handed to a single-precision plan is read as
/// twice as many wrong numbers.
enum class FftwPrecision : std::uint8_t
{
    /// `fftwf_`: elements are `float`.
    Single,

    /// `fftw_`: elements are `double`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Double
};

/// Whether an array holds one real per element or two adjacent reals.
///
/// FFTW's complex type is two reals side by side, so the distinction is a stride and a count rather
/// than a different kind of memory, which is what lets this engine describe both without naming
/// either FFTW type.
enum class FftwArrayKind : std::uint8_t
{
    /// One real number per element.
    Real,

    /// Two adjacent reals per element, real part first - the layout of `fftw_complex` and
    /// `fftwf_complex`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    InterleavedComplex
};

/// Whether the transform writes its output over its input.
///
/// The field that decides what a real array's row must be padded to, and it is stated rather than
/// inferred because the same buffer is correct for one answer and wrong for the other.
enum class FftwPlacement : std::uint8_t
{
    /// Input and output are different arrays; a real row holds exactly its own samples.
    OutOfPlace,

    /// Output is written over the input; a real row must hold `2 * (width / 2 + 1)` reals so the
    /// complex result fits where the real input was.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    InPlace
};

/// The alignment `fftw_malloc` guarantees on the builds this repository uses, in bytes.
///
/// Stated as a default rather than as a requirement. FFTW plans created without `FFTW_UNALIGNED`
/// remember the alignment of the arrays they were planned over, and an AVX build wants more than
/// this - so the caller passes what its own plan needs and this is only what it gets when it has no
/// reason to ask for anything else.
inline constexpr std::uint64_t kFftwDefaultAlignmentBytes = 16u;

/// One FFTW array's worth of description: the base pointer and the numbers a plan is built from.
///
/// Dimensions are in elements and the row stride in reals, never in bytes, and all three are `int`,
/// which is the type FFTW takes them in. That the engine counts strides in bytes and FFTW does not is
/// precisely the conversion this description exists to do once, checked, instead of at every call
/// site.
struct FftwArrayDescription
{
    /// The first element. Never null in a description this engine produced. A caller with the FFTW
    /// headers reinterprets it as `double*`, `float*`, `fftw_complex*` or `fftwf_complex*`
    /// according to `precision` and `kind`.
    void* data;

    /// Which precision the elements are.
    FftwPrecision precision;

    /// Whether one element is one real or two.
    FftwArrayKind kind;

    /// The slow dimension: how many rows the transform sees.
    std::int32_t n0;

    /// The fast dimension: how many elements of `kind` there are along a row. For a real array this
    /// is the transform width; for the Hermitian half-spectrum of a real transform it is
    /// `width / 2 + 1`.
    std::int32_t n1;

    /// Distance from one row to the next, counted in reals. Counted in reals rather than in
    /// elements so that one field describes both kinds without the reader having to remember which
    /// one is in force.
    std::int32_t rowStrideReals;

    /// Whether the base address meets the alignment the caller asked for.
    bool alignmentSatisfied;

    /// Whether two descriptions describe the same array.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when all seven fields are equal.
    [[nodiscard]] friend constexpr bool operator==(const FftwArrayDescription& left,
                                                   const FftwArrayDescription& right) noexcept = default;
};

/// Whether a value is one of the precisions.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param precision the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isFftwPrecision(FftwPrecision precision) noexcept
{
    return static_cast<std::uint8_t>(precision) <= static_cast<std::uint8_t>(FftwPrecision::Double);
}

/// Whether a value is one of the array kinds.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isFftwArrayKind(FftwArrayKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(FftwArrayKind::InterleavedComplex);
}

/// Whether a value is one of the placements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param placement the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isFftwPlacement(FftwPlacement placement) noexcept
{
    return static_cast<std::uint8_t>(placement) <= static_cast<std::uint8_t>(FftwPlacement::InPlace);
}

/// The precision an engine sample type is, when FFTW has one for it.
///
/// A closed table with six gaps: FFTW transforms floating-point data and nothing else, so every
/// integer representation this engine stores has no answer here and has to be converted first. That
/// is a real refusal rather than an omission - a transform over integer codes is a transform over a
/// different signal.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type the engine representation.
/// @return the precision, or an empty optional when FFTW does not transform that representation.
[[nodiscard]] constexpr std::optional<FftwPrecision> fftwPrecisionOf(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::Float32:
        return FftwPrecision::Single;
    case SampleType::Float64:
        return FftwPrecision::Double;
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

/// The engine representation one real of an FFTW array is.
///
/// The other direction of the same table.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param precision the FFTW precision.
/// @return the representation, or an empty optional for a value outside the enumeration.
[[nodiscard]] constexpr std::optional<SampleType> fftwSampleTypeOf(FftwPrecision precision) noexcept
{
    switch (precision)
    {
    case FftwPrecision::Single:
        return SampleType::Float32;
    case FftwPrecision::Double:
        return SampleType::Float64;
    }
    return std::nullopt;
}

/// How many complex elements the Hermitian half-spectrum of a real transform of this width holds.
///
/// `width / 2 + 1`, which is FFTW's own rule: a real signal's spectrum is conjugate-symmetric, so
/// half of it plus the zero frequency is the whole of the information.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param width the real transform width.
/// @return the number of complex elements in one row of the half-spectrum; zero for a zero width.
[[nodiscard]] constexpr std::uint32_t fftwHermitianColumns(std::uint32_t width) noexcept
{
    return width == 0u ? 0u : width / 2u + 1u;
}

/// How many reals one row of a real array must hold for a transform of this width and placement.
///
/// The number an in-place real-to-complex transform gets wrong when nobody states the placement:
/// the complex output is written over the real input, so the row has to be long enough for the
/// output even while it holds the input.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param width the real transform width.
/// @param placement whether the output is written over the input.
/// @return `width` out of place, and `2 * (width / 2 + 1)` in place.
[[nodiscard]] constexpr std::uint64_t fftwRealRowReals(std::uint32_t width, FftwPlacement placement) noexcept
{
    if (placement == FftwPlacement::InPlace)
    {
        return 2u * static_cast<std::uint64_t>(fftwHermitianColumns(width));
    }
    return width;
}

/// Whether one plane's samples can be transformed by FFTW where they sit, without a copy.
///
/// The layout half of the bridge. What it comes to: FFTW counts in elements and indexes with `int`,
/// so the plane's row stride has to be a whole number of reals and every dimension has to fit; the
/// plane has to be one real per sample, because a plane whose pixels interleave two channels is not
/// a signal FFTW was handed; and the row has to be long enough for the placement.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param type what one sample is.
/// @param layout where the samples sit.
/// @param placement whether the transform writes its output over its input.
/// @return `BridgeRefusal::None` when a plan can be built over those samples; otherwise the first
///         rule that refused: `UnknownContract` for a placement outside the enumeration,
///         `SampleTypeNotRepresentable` for an integer representation or a layout whose element
///         size is not the sample's, `ArrangementNotRepresentable` for more than one sample per
///         pixel, `ExtentExceedsLibraryIndex` for a dimension beyond `int`,
///         `RowStrideNotRepresentable` for a stride that is not a whole number of reals or does not
///         fit `int`, and `ArrangementNotRepresentable` again for a row too short for the
///         placement.
[[nodiscard]] constexpr BridgeRefusal checkFftwGeometry(SampleType type, const ImageLayout& layout,
                                                        FftwPlacement placement) noexcept
{
    if (!isFftwPlacement(placement))
    {
        return BridgeRefusal::UnknownContract;
    }
    if (!fftwPrecisionOf(type).has_value())
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
    constexpr auto kMaxIndex = static_cast<std::uint64_t>(0x7fffffff);
    if (layout.extent().width() > kMaxIndex || layout.extent().height() > kMaxIndex)
    {
        return BridgeRefusal::ExtentExceedsLibraryIndex;
    }

    const auto elementSize = static_cast<std::uint64_t>(layout.elementSizeBytes());
    if (layout.rowStrideBytes() % elementSize != 0u)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    const std::uint64_t rowStrideReals = layout.rowStrideBytes() / elementSize;
    if (rowStrideReals > kMaxIndex)
    {
        return BridgeRefusal::RowStrideNotRepresentable;
    }
    if (rowStrideReals < fftwRealRowReals(layout.extent().width(), placement))
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    return BridgeRefusal::None;
}

/// Whether this plane can be handed to FFTW under this contract, without copying.
///
/// The whole bridge: the claim, then the base address against the alignment the caller's plan was
/// created for, then the layout. Alignment is checked here rather than in the geometry half because
/// it is a property of the pointer rather than of the shape, and because a caller that planned with
/// `FFTW_UNALIGNED` states an alignment of one and is refused for nothing.
///
/// Thread-safe and allocation-free. Not usable in a constant expression, because testing an
/// address's alignment needs the address as an integer.
///
/// @tparam SampleT the plane's sample type; a const one describes read-only samples.
/// @param plane the samples to transform.
/// @param contract what the bridge is being told about them.
/// @param placement whether the transform writes its output over its input.
/// @param requiredAlignmentBytes what the caller's plan requires of the base address; a power of
///        two, one for a plan created with `FFTW_UNALIGNED`, and `kFftwDefaultAlignmentBytes` for
///        an ordinary SIMD plan over `fftw_malloc` memory.
/// @return `BridgeRefusal::None` when the samples can be transformed where they are; otherwise the
///         first rule that refused: whatever `checkExternalBufferContract` reports,
///         `AlignmentInsufficient` for a base address that misses the alignment or an alignment
///         that is not a power of two, or whatever `checkFftwGeometry` reports.
template <ViewableSample SampleT>
[[nodiscard]] inline BridgeRefusal checkFftwArray(const PlaneView<SampleT>& plane,
                                                  const ExternalBufferContract& contract, FftwPlacement placement,
                                                  std::uint64_t requiredAlignmentBytes) noexcept
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
    if (!isPowerOfTwo(requiredAlignmentBytes) || !isPointerAligned(plane.data(), requiredAlignmentBytes))
    {
        return BridgeRefusal::AlignmentInsufficient;
    }
    return checkFftwGeometry(PlaneView<SampleT>::kSampleType, plane.layout(), placement);
}

/// The array description for one plane of reals, when the bridge holds.
///
/// The caller builds its plan from what comes back: `n0` and `n1` are the transform dimensions and
/// `rowStrideReals` is the physical length of a row, which FFTW's advanced interface takes as the
/// fast entry of the array's embedding and its basic two-dimensional real interface assumes to be
/// `n1` out of place or the padded length in place.
///
/// The description borrows: it points at the plane's samples and owns nothing, so it must not
/// outlive them, which is what `BorrowScope` in the contract is for.
///
/// Thread-safe, allocation-free.
///
/// @tparam SampleT the plane's sample type; a const one describes read-only samples.
/// @param plane the samples to transform.
/// @param contract what the bridge is being told about them.
/// @param placement whether the transform writes its output over its input.
/// @param requiredAlignmentBytes what the caller's plan requires of the base address.
/// @return the description, or an empty optional when any rule refuses.
template <ViewableSample SampleT>
[[nodiscard]] inline std::optional<FftwArrayDescription>
describeAsFftwReal(const PlaneView<SampleT>& plane, const ExternalBufferContract& contract, FftwPlacement placement,
                   std::uint64_t requiredAlignmentBytes) noexcept
{
    if (checkFftwArray(plane, contract, placement, requiredAlignmentBytes) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    const std::optional<FftwPrecision> precision = fftwPrecisionOf(PlaneView<SampleT>::kSampleType);
    if (!precision.has_value())
    {
        return std::nullopt;
    }
    const auto rowStrideReals = static_cast<std::int32_t>(plane.rowStrideBytes() / plane.layout().elementSizeBytes());
    return FftwArrayDescription{.data = const_cast<std::remove_const_t<SampleT>*>(plane.data()),
                                .precision = *precision,
                                .kind = FftwArrayKind::Real,
                                .n0 = static_cast<std::int32_t>(plane.height()),
                                .n1 = static_cast<std::int32_t>(plane.width()),
                                .rowStrideReals = rowStrideReals,
                                .alignmentSatisfied = true};
}

/// The array description for one plane read as an interleaved complex spectrum.
///
/// A plane of `2 * columns` reals per row is a plane of `columns` complex elements, and this is the
/// one place that arithmetic is done. The plane's own width is in reals, so it must be even and the
/// description reports half of it.
///
/// Thread-safe, allocation-free.
///
/// @tparam SampleT the plane's sample type; a const one describes read-only samples.
/// @param plane the samples to transform, whose width counts reals rather than complex elements.
/// @param contract what the bridge is being told about them.
/// @param requiredAlignmentBytes what the caller's plan requires of the base address.
/// @return the description, or an empty optional when any rule refuses or the plane's width is odd,
///         which is a plane that cannot be read as complex elements at all.
template <ViewableSample SampleT>
[[nodiscard]] inline std::optional<FftwArrayDescription>
describeAsFftwComplex(const PlaneView<SampleT>& plane, const ExternalBufferContract& contract,
                      std::uint64_t requiredAlignmentBytes) noexcept
{
    if (plane.width() % 2u != 0u)
    {
        return std::nullopt;
    }
    std::optional<FftwArrayDescription> description =
        describeAsFftwReal(plane, contract, FftwPlacement::OutOfPlace, requiredAlignmentBytes);
    if (!description.has_value())
    {
        return std::nullopt;
    }
    description->kind = FftwArrayKind::InterleavedComplex;
    description->n1 = static_cast<std::int32_t>(plane.width() / 2u);
    return description;
}

/// The engine layout an FFTW array's reals already have.
///
/// What makes the copy path explicit rather than implied. An array this engine cannot share - the
/// wrong precision for the image, most often - is still describable as an engine layout, so the
/// caller builds a view over FFTW's memory and moves the samples with `Image/ImageConversion.h`,
/// where the decisions a conversion makes are stated and the cost is visible.
///
/// The layout describes one plane of reals: a complex array is `2 * n1` reals per row, which is
/// what a view over it has to say if a typed pointer is to reach every one of them.
///
/// Thread-safe and allocation-free.
///
/// @param description the array, as a plan's own numbers report it.
/// @param limits the geometry limits in force, which the resulting extent is checked against.
/// @return the layout, or an empty optional when the description is not one an engine view can
///         describe - a non-positive dimension, a stride shorter than a row, an extent outside the
///         limits.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageLayout> fftwArrayLayout(const FftwArrayDescription& description,
                                                                              const EngineLimits& limits) noexcept;

/// Whether an array a plan owns can be viewed by this engine as it stands.
///
/// The reverse direction. FFTW's arrays come from `fftw_malloc` and are released with `fftw_free`,
/// so a view over them is a borrow whatever the engine does with it. An FFTW array is always
/// writable through its own pointer, so the contract's mutability never refuses here.
///
/// Thread-safe and allocation-free.
///
/// @param description the array, as a plan's own numbers report it.
/// @param contract what the bridge is being told about it.
/// @return `BridgeRefusal::None` when a view can be built over it; otherwise the first rule that
///         refused: whatever `checkExternalBufferContract` reports, `UnknownContract` for a
///         precision or kind outside its enumeration, `NoSamples` for a null base,
///         `ExtentExceedsLibraryIndex` for a dimension that is not positive,
///         `RowStrideNotRepresentable` for a stride that is not positive or is shorter than a row,
///         or `AlignmentInsufficient` when the description says the alignment was not met.
[[nodiscard]] AFTERNIGHTENGINE_API BridgeRefusal adoptFftwArrayCheck(const FftwArrayDescription& description,
                                                                     const ExternalBufferContract& contract) noexcept;

} // namespace an
