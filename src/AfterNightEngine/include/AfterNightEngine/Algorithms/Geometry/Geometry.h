// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image-level crop, resize, integer resample, rotation, warp and orientation plans.
///
/// **A plan, then an execution.** A `plan*` builder checks a request against a source descriptor
/// and returns a `GeometryPlan` holding the destination descriptor and a destination-to-source
/// matrix. The plan is an allocation-free value that may be reused, concurrently too, over every
/// image the source descriptor describes. `transformImageGeometry` executes one over every channel of
/// an image, and first rebuilds it through its builder, so a plan no builder would have produced is
/// refused rather than executed.
///
/// **Coordinates.** Pixel centers have integer coordinates. Every general transform stores a
/// destination-to-source matrix, because sampling asks where an output came from and never needs to
/// invert a matrix while writing pixels. Resize uses the half-pixel convention: destination center
/// `(x, y)` maps to `((x + 0.5) * source/destination - 0.5)` on each axis, except `Nearest`, whose
/// asymmetric source index is `floor(x * source/destination)`. A `NearestExact` resize evaluates the
/// half-pixel rule in a 16-bit fixed-point coordinate. A projective denominator is checked for every
/// destination coordinate before the first sample is written.
///
/// **Exact and interpolating operations.** Crop, integer resample and orientation keep stored values
/// in their own representation, any of the eight, and a reduction's average or median is rounded
/// back to it. Resize, rotation and the two warps sample through the sampling kernel's
/// interpolation, border and invalid-neighbour rules over Float32 working planes, accumulate in
/// Float64, and write back through the default code-value conversion. `NearestExact` copies a stored
/// value instead, and `LinearExact` computes in the representation's own integers for UInt8, UInt16
/// and Int16. Only the crop, the CFA-aware integer resample and the orientation accept an unresolved
/// mosaic. `GeometryResult::exactStoredValues` says which kind of path ran.
///
/// **Validity.** The source may state any validity rule. The destination's validity is regenerated
/// into a packed mask the caller supplies: a set bit is a sample the operation wrote, and an
/// unwritten sample keeps whatever the destination held.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace an
{

/// Which geometry algorithm a reusable plan contains.
enum class GeometryOperation : std::uint8_t
{
    /// Exact rectangular subset.
    Crop = 0u,

    /// Arbitrary-size resize under the interpolation mode's stated coordinate convention.
    Resize = 1u,

    /// Equal-axis integer enlargement or reduction.
    IntegerResample = 2u,

    /// Integer enlargement or reduction over one unresolved CFA plane.
    CfaIntegerResample = 3u,

    /// Rotation about the source image's center.
    Rotate = 4u,

    /// Destination-to-source affine warp.
    AffineWarp = 5u,

    /// Destination-to-source projective warp.
    ProjectiveWarp = 6u,

    /// Exact right-angle turn or mirror, which moves every sample without interpolating it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Orient = 7u
};

/// Whether a value names a geometry operation this build implements.
///
/// @param operation the value to inspect.
/// @return true for each declared operation.
[[nodiscard]] constexpr bool isGeometryOperation(GeometryOperation operation) noexcept
{
    return static_cast<std::uint8_t>(operation) <= static_cast<std::uint8_t>(GeometryOperation::Orient);
}

/// Source validity borrowed by one geometry execution.
struct GeometrySelection
{
    /// The source's validity rule.
    ValidityDescriptor validity{};

    /// Packed source bits exactly when `validity` names an explicit mask: one plane every channel
    /// shares, or one per channel. Borrowed for the call.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;
};

/// How a crop handles a rectangle extending beyond the source's right or bottom edge.
enum class CropBoundsPolicy : std::uint8_t
{
    /// Refuse any requested rectangle that is not fully contained by the source.
    Reject = 0u,

    /// Keep the in-bounds intersection beginning at `(x, y)`.
    ClipToSource = 1u
};

/// Whether a value names a crop-bounds rule.
///
/// @param policy the value to inspect.
/// @return true for strict rejection or clipping to the source.
[[nodiscard]] constexpr bool isCropBoundsPolicy(CropBoundsPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(CropBoundsPolicy::ClipToSource);
}

/// An exact crop rectangle, in source pixel coordinates.
struct CropRequest
{
    /// First source column.
    std::uint32_t x = 0u;

    /// First source row.
    std::uint32_t y = 0u;

    /// Number of columns in the result, at least one.
    std::uint32_t width = 1u;

    /// Number of rows in the result, at least one.
    std::uint32_t height = 1u;

    /// Whether an overhanging right or bottom edge is refused or clipped.
    CropBoundsPolicy bounds = CropBoundsPolicy::Reject;
};

/// What an arbitrary resize does with dark lobes introduced by a high-order kernel.
enum class ResizeRingingPolicy : std::uint8_t
{
    /// Keep the interpolation result, including legitimate negative or overshooting lobes.
    Keep = 0u,

    /// Compare each bicubic or Lanczos output with an area guide - the area average of its source
    /// footprint when neither axis grows, a bilinear sample otherwise - and replace an output below
    /// 98 per cent of its guide, together with its four neighbours, by their guides.
    SuppressDarkRinging = 1u
};

/// Whether a value names a resize-ringing rule.
///
/// @param policy the value to inspect.
/// @return true for retaining the interpolation or applying the dark-ringing guard.
[[nodiscard]] constexpr bool isResizeRingingPolicy(ResizeRingingPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(ResizeRingingPolicy::SuppressDarkRinging);
}

/// An arbitrary-size resize request.
struct ResizeRequest
{
    /// Destination columns, at least one.
    std::uint32_t width = 1u;

    /// Destination rows, at least one.
    std::uint32_t height = 1u;

    /// Interpolation, border and invalid-neighbour behavior.
    SamplingKernelParameters sampling{.interpolation = KernelInterpolation::Linear,
                                      .border = KernelBorderMode::ReflectExcludingEdge,
                                      .invalid = SamplingInvalidPolicy::RequireAll};

    /// Whether bicubic and Lanczos results use the area-guide dark-ringing guard; every other
    /// interpolation ignores it.
    ResizeRingingPolicy ringing = ResizeRingingPolicy::SuppressDarkRinging;
};

/// Whether an integer factor makes an image larger or smaller.
enum class IntegerResampleDirection : std::uint8_t
{
    /// Replicate each source position into a factor by factor destination block.
    Enlarge = 0u,

    /// Combine a factor by factor source block into one destination position.
    Reduce = 1u
};

/// Whether a value names an integer-resample direction.
///
/// @param direction the value to inspect.
/// @return true for enlargement or reduction.
[[nodiscard]] constexpr bool isIntegerResampleDirection(IntegerResampleDirection direction) noexcept
{
    return static_cast<std::uint8_t>(direction) <= static_cast<std::uint8_t>(IntegerResampleDirection::Reduce);
}

/// Reducer used by an integer downsample.
enum class IntegerReduction : std::uint8_t
{
    /// Arithmetic mean accumulated in Float64.
    Average = 0u,

    /// Middle value, averaging the two middle values for an even count.
    Median = 1u,

    /// Largest valid input.
    Maximum = 2u,

    /// Smallest valid input.
    Minimum = 3u
};

/// How integer reduction handles an incomplete block at the source's right or bottom edge.
enum class IntegerRemainderPolicy : std::uint8_t
{
    /// Refuse a source axis that is not divisible by the reduction factor.
    RequireCompleteBlocks = 0u,

    /// Reduce every complete block and discard trailing samples that cannot fill one.
    DiscardPartialEdges = 1u
};

/// Whether a value names an integer-reduction remainder rule.
///
/// @param policy the value to inspect.
/// @return true for strict divisibility or discarded partial edges.
[[nodiscard]] constexpr bool isIntegerRemainderPolicy(IntegerRemainderPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(IntegerRemainderPolicy::DiscardPartialEdges);
}

/// Whether a value names an integer reduction this build implements.
///
/// @param reduction the value to inspect.
/// @return true for every declared reducer.
[[nodiscard]] constexpr bool isIntegerReduction(IntegerReduction reduction) noexcept
{
    return static_cast<std::uint8_t>(reduction) <= static_cast<std::uint8_t>(IntegerReduction::Minimum);
}

/// An equal-axis integer resample request.
struct IntegerResampleRequest
{
    /// Integer scale, at least one and bounded by the source or representable destination extent.
    std::uint32_t factor = 2u;

    /// Whether the result grows or shrinks.
    IntegerResampleDirection direction = IntegerResampleDirection::Enlarge;

    /// How a reduction combines its valid inputs; ignored by enlargement. Invalid inputs are
    /// omitted, and an output with no valid contributor is marked invalid and left unwritten. The
    /// answer returns to the stored representation through the default code-value conversion, which
    /// rounds an average or an even-count median to nearest, ties to even.
    IntegerReduction reduction = IntegerReduction::Average;

    /// What a reduction does with incomplete trailing blocks; ignored by enlargement.
    IntegerRemainderPolicy remainder = IntegerRemainderPolicy::RequireCompleteBlocks;
};

/// Whether rotation retains the source extent or expands to contain every rotated pixel cell.
enum class RotationCanvas : std::uint8_t
{
    /// Keep the source width and height, cropping rotated corners when necessary.
    Preserve = 0u,

    /// Build the smallest centered integer extent containing the rotated source pixel cells.
    ExpandToFit = 1u
};

/// Whether a value names a rotation-canvas rule.
///
/// @param canvas the value to inspect.
/// @return true for preserving or expanding the canvas.
[[nodiscard]] constexpr bool isRotationCanvas(RotationCanvas canvas) noexcept
{
    return static_cast<std::uint8_t>(canvas) <= static_cast<std::uint8_t>(RotationCanvas::ExpandToFit);
}

/// A rotation about the source image's pixel-center midpoint.
struct RotationRequest
{
    /// Clockwise angle in radians in the image's x-right, y-down coordinate system.
    double angleRadians = 0.0;

    /// Whether the destination keeps the source extent or expands around the same center.
    RotationCanvas canvas = RotationCanvas::Preserve;

    /// Interpolation, border and invalid-neighbour behavior.
    SamplingKernelParameters sampling{.interpolation = KernelInterpolation::Linear,
                                      .border = KernelBorderMode::Constant,
                                      .invalid = SamplingInvalidPolicy::RequireAll};
};

/// A destination-sized affine warp.
struct AffineWarpRequest
{
    /// Destination columns, at least one.
    std::uint32_t width = 1u;

    /// Destination rows, at least one.
    std::uint32_t height = 1u;

    /// Row-major two-by-three matrix mapping destination pixel centers to source pixel centers.
    std::array<double, 6> destinationToSource{1.0, 0.0, 0.0, 0.0, 1.0, 0.0};

    /// Pixel-width to pixel-height ratio the destination descriptor states, finite and above zero.
    ///
    /// It does not enter the sampling, which the matrix alone decides.
    double destinationPixelAspectRatio = 1.0;

    /// Interpolation, border and invalid-neighbour behavior.
    SamplingKernelParameters sampling{.interpolation = KernelInterpolation::Linear,
                                      .border = KernelBorderMode::Constant,
                                      .invalid = SamplingInvalidPolicy::RequireAll};
};

/// A destination-sized projective warp.
struct ProjectiveWarpRequest
{
    /// Destination columns, at least one.
    std::uint32_t width = 1u;

    /// Destination rows, at least one.
    std::uint32_t height = 1u;

    /// Row-major three-by-three homogeneous matrix mapping destination centers to source centers.
    std::array<double, 9> destinationToSource{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    /// Pixel-width to pixel-height ratio the destination descriptor states, finite and above zero.
    ///
    /// It does not enter the sampling, which the matrix alone decides.
    double destinationPixelAspectRatio = 1.0;

    /// Interpolation, border and invalid-neighbour behavior.
    SamplingKernelParameters sampling{.interpolation = KernelInterpolation::Linear,
                                      .border = KernelBorderMode::Constant,
                                      .invalid = SamplingInvalidPolicy::RequireAll};
};

/// One of the eight ways a stored picture is turned or mirrored to be shown the right way up.
///
/// Numbered as the Exif and TIFF `Orientation` tag numbers them, less one, so the tag a file carries
/// names the transform that presents it: a camera held on its side writes tag 6, which is
/// `Rotate90Clockwise`. Every one is exact - each destination sample is one source sample - and every
/// one keeps a mosaic's filters under the samples they measured.
enum class ImageOrientation : std::uint8_t
{
    /// Tag 1: the picture is stored as it is shown.
    Identity = 0u,

    /// Tag 2: mirrored left to right.
    MirrorHorizontal = 1u,

    /// Tag 3: turned half a turn.
    Rotate180 = 2u,

    /// Tag 4: mirrored top to bottom.
    MirrorVertical = 3u,

    /// Tag 5: mirrored across the diagonal through the top-left corner, which swaps width and height.
    Transpose = 4u,

    /// Tag 6: turned a quarter turn clockwise, which swaps width and height.
    Rotate90Clockwise = 5u,

    /// Tag 7: mirrored across the diagonal through the top-right corner, which swaps width and height.
    Transverse = 6u,

    /// Tag 8: turned a quarter turn anticlockwise, which swaps width and height.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Rotate270Clockwise = 7u
};

/// Whether a value names an orientation this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param orientation the value to inspect.
/// @return true for each of the eight orientations.
[[nodiscard]] constexpr bool isImageOrientation(ImageOrientation orientation) noexcept
{
    return static_cast<std::uint8_t>(orientation) <= static_cast<std::uint8_t>(ImageOrientation::Rotate270Clockwise);
}

/// Whether an orientation exchanges the picture's width and height.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param orientation the orientation to inspect.
/// @return true for the two diagonal mirrors and the two quarter turns; false for the other four and
///         for a value outside the enumeration.
[[nodiscard]] constexpr bool imageOrientationSwapsAxes(ImageOrientation orientation) noexcept
{
    return isImageOrientation(orientation) &&
           static_cast<std::uint8_t>(orientation) >= static_cast<std::uint8_t>(ImageOrientation::Transpose);
}

/// The orientation an Exif or TIFF `Orientation` tag asks for.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param tag the tag's value as the file stored it.
/// @return the orientation for a value from one through eight; empty for any other value, which the
///         standard does not define and which a caller shows as stored or refuses by its own rule.
[[nodiscard]] constexpr std::optional<ImageOrientation> imageOrientationFromExif(std::uint32_t tag) noexcept
{
    if (tag < 1u || tag > 8u)
    {
        return std::nullopt;
    }
    return static_cast<ImageOrientation>(tag - 1u);
}

/// The orientation a camera raw decoder's flip code asks for.
///
/// The code `RawCameraMetadata::orientation` retains: bit value four exchanges the axes first, two
/// then mirrors top to bottom and one mirrors left to right, so five is a quarter turn anticlockwise
/// and six a quarter turn clockwise.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param flip the decoder's flip code.
/// @return the orientation for a code from zero through seven; empty for any other value.
[[nodiscard]] constexpr std::optional<ImageOrientation> imageOrientationFromRawFlip(std::int32_t flip) noexcept
{
    switch (flip)
    {
    case 0:
        return ImageOrientation::Identity;
    case 1:
        return ImageOrientation::MirrorHorizontal;
    case 2:
        return ImageOrientation::MirrorVertical;
    case 3:
        return ImageOrientation::Rotate180;
    case 4:
        return ImageOrientation::Transpose;
    case 5:
        return ImageOrientation::Rotate270Clockwise;
    case 6:
        return ImageOrientation::Rotate90Clockwise;
    case 7:
        return ImageOrientation::Transverse;
    default:
        return std::nullopt;
    }
}

/// Largest integer scale representable by the public request; geometry and memory still bound it.
inline constexpr std::uint32_t kMaxIntegerResampleFactor = (std::numeric_limits<std::uint32_t>::max)();

/// A validated, reusable geometry transform.
///
/// An allocation-free value a `plan*` builder produces and `transformImageGeometry` executes, over
/// any number of images its source descriptor describes.
class GeometryPlan
{
public:
    /// Records a plan whose request has already been checked.
    ///
    /// Public so the value remains an ordinary exported value record; callers normally use one of
    /// the `plan*` builders. `transformImageGeometry` rebuilds every plan through the builder its
    /// operation names from the request the plan records, and refuses one that differs - with
    /// `ErrorCode::IncompatibleArguments`, or with the builder's own refusal of that request - so a
    /// plan recorded here executes only when it is exactly what that builder returns.
    ///
    /// @param operation which transform runs.
    /// @param sourceDescriptor source geometry and meaning used to build the plan.
    /// @param destinationDescriptor geometry and meaning the result will have.
    /// @param destinationToSource row-major projective matrix under the pixel-center convention.
    /// @param sampling coordinate-sampling policy for non-exact transforms.
    /// @param cropX source crop origin or zero.
    /// @param cropY source crop origin or zero.
    /// @param integerRequest integer-resample policy or its default.
    /// @param resizeRinging resize dark-ringing policy, or `Keep` for another operation.
    AFTERNIGHTENGINE_API GeometryPlan(GeometryOperation operation, const ImageDescriptor& sourceDescriptor,
                                      const ImageDescriptor& destinationDescriptor,
                                      const std::array<double, 9>& destinationToSource,
                                      const SamplingKernelParameters& sampling, std::uint32_t cropX,
                                      std::uint32_t cropY, const IntegerResampleRequest& integerRequest,
                                      ResizeRingingPolicy resizeRinging = ResizeRingingPolicy::Keep) noexcept;

    /// Which transform the plan executes.
    ///
    /// @return the checked operation.
    [[nodiscard]] AFTERNIGHTENGINE_API GeometryOperation operation() const noexcept;

    /// Source description against which the plan was built.
    ///
    /// @return the complete checked source descriptor.
    [[nodiscard]] AFTERNIGHTENGINE_API const ImageDescriptor& sourceDescriptor() const noexcept;

    /// Description the destination must have.
    ///
    /// @return the transformed extent, CFA phase and pixel aspect with other meaning preserved.
    [[nodiscard]] AFTERNIGHTENGINE_API const ImageDescriptor& destinationDescriptor() const noexcept;

    /// Matrix used by resize and warp execution.
    ///
    /// @return row-major destination-to-source homogeneous coefficients.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::array<double, 9>& destinationToSource() const noexcept;

    /// Sampling policy used by non-exact transforms.
    ///
    /// @return interpolation, border and invalid-neighbour policy.
    [[nodiscard]] AFTERNIGHTENGINE_API const SamplingKernelParameters& sampling() const noexcept;

    /// Horizontal crop origin stored by an exact crop.
    ///
    /// @return the source column, or zero for another operation.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t cropX() const noexcept;

    /// Vertical crop origin stored by an exact crop.
    ///
    /// @return the source row, or zero for another operation.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t cropY() const noexcept;

    /// Integer-resample policy stored by an integer plan.
    ///
    /// @return factor, direction, reducer and remainder rule, or the default for another operation.
    [[nodiscard]] AFTERNIGHTENGINE_API const IntegerResampleRequest& integerRequest() const noexcept;

    /// Resize dark-ringing policy stored by a resize plan.
    ///
    /// @return the checked policy, or `Keep` for another operation.
    [[nodiscard]] AFTERNIGHTENGINE_API ResizeRingingPolicy resizeRinging() const noexcept;

private:
    /// The operation kind.
    GeometryOperation m_operation;

    /// Source description.
    ImageDescriptor m_sourceDescriptor;

    /// Destination description.
    ImageDescriptor m_destinationDescriptor;

    /// Destination-to-source homogeneous matrix.
    std::array<double, 9> m_destinationToSource;

    /// Sampling policy.
    SamplingKernelParameters m_sampling;

    /// Exact crop's source column.
    std::uint32_t m_cropX;

    /// Exact crop's source row.
    std::uint32_t m_cropY;

    /// Integer factor, direction and reducer.
    IntegerResampleRequest m_integerRequest;

    /// Resize ringing control, or `Keep` for an operation that does not resize.
    ResizeRingingPolicy m_resizeRinging;
};

/// Builds an exact crop plan, shifting a mosaic's phase by the crop origin.
///
/// The phase is reduced over the mosaic's own repeating block - two pixels for a named pattern and
/// six for a tabulated one, whose table the destination keeps - so every destination pixel reports
/// the filter its source pixel was measured under. Under `CropBoundsPolicy::ClipToSource` the
/// destination is the part of the rectangle the source holds.
///
/// @param source source descriptor.
/// @param request non-empty rectangle and its overhang policy.
/// @return the resolved plan, or `ErrorCode::InvalidArgument` when the rectangle is empty, begins
///         outside the source, overhangs under `CropBoundsPolicy::Reject`, or names an unknown rule.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryPlan> planCrop(const ImageDescriptor& source,
                                                                 const CropRequest& request);

/// Builds a resize plan under the interpolation mode's coordinate convention.
///
/// Generic interpolation over an unresolved mosaic is refused because it would combine different
/// filters; use `planCfaIntegerResample` for that image. Every mode uses half-pixel centers except
/// `KernelInterpolation::Nearest`, which uses an asymmetric floor mapping.
/// `KernelInterpolation::NearestExact` evaluates its nearest-center counterpart with a 16-bit
/// fixed-point coordinate accumulator. `KernelInterpolation::LinearExact` uses fixed-point
/// coefficients and output rounding - OpenCV's `INTER_LINEAR_EXACT` rule - for UInt8, UInt16 and
/// Int16 clamp-border resizes under `SamplingInvalidPolicy::RequireAll`, and ordinary floating linear
/// interpolation for every other resident representation.
///
/// `KernelInterpolation::Area` integrates each destination cell's source footprint when neither axis
/// grows, and otherwise samples bilinearly under the half-pixel rule rather than replicating samples.
/// The destination's pixel aspect is the source's multiplied by the horizontal scale over the
/// vertical one, so a resize that stretches one axis more than the other says so.
///
/// @param source source descriptor.
/// @param request destination extent and sampling policy.
/// @return the plan; `ErrorCode::IncompatibleArguments` for an unresolved mosaic;
///         `ErrorCode::InvalidArgument` for a sampling policy with an unknown rule,
///         `KernelBorderMode::NotApplicable` or a substitute value that is not finite, a ringing
///         policy outside its enumeration, or a zero destination extent; or
///         `ErrorCode::OutOfRange` for a destination extent beyond `defaultEngineLimits()`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryPlan> planResize(const ImageDescriptor& source,
                                                                   const ResizeRequest& request);

/// Builds a generic integer enlargement or reduction plan.
///
/// An enlargement multiplies both axes by the factor. A reduction divides them by it, rounding down
/// under `IntegerRemainderPolicy::DiscardPartialEdges`.
///
/// @param source source descriptor, which must not be mosaiced.
/// @param request factor, direction and reducer.
/// @return the plan; `ErrorCode::IncompatibleArguments` for an unresolved mosaic, which
///         `planCfaIntegerResample` serves, or a strict reduction whose factor does not divide both
///         axes; `ErrorCode::InvalidArgument` for a direction, reducer or remainder rule outside its
///         enumeration; or `ErrorCode::OutOfRange` for a zero factor, a reduction factor larger than
///         either axis, or a destination extent that does not fit 32 bits or lies beyond
///         `defaultEngineLimits()`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryPlan> planIntegerResample(const ImageDescriptor& source,
                                                                            const IntegerResampleRequest& request);

/// Builds a CFA-aware integer enlargement or reduction plan.
///
/// Enlargement replicates within each two-by-two phase lattice. Reduction combines only samples
/// under the same filter. Both source axes must be even; a strict reduction requires each to be
/// divisible by `2 * factor`, and the compatibility remainder policy discards incomplete phase
/// blocks. The pattern and its phase are preserved.
///
/// @param source unresolved two-by-two mosaic descriptor.
/// @param request factor, direction and reducer.
/// @return the plan; `ErrorCode::IncompatibleArguments` for a source that is not an unresolved
///         mosaic, a mosaic that does not repeat over two pixels - a tabulated one - an odd width
///         or height, or a strict reduction whose axes are not both divisible by `2 * factor`;
///         `ErrorCode::InvalidArgument` for a rule outside its enumeration; or
///         `ErrorCode::OutOfRange` for a zero factor, a reduction for which `2 * factor` exceeds
///         either axis, or a destination extent that does not fit 32 bits or lies beyond
///         `defaultEngineLimits()`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryPlan> planCfaIntegerResample(const ImageDescriptor& source,
                                                                               const IntegerResampleRequest& request);

/// Builds a rotation about the source midpoint, retaining or expanding its canvas by request.
///
/// The source's midpoint maps to the destination's, so an expanded canvas grows equally on both
/// sides of each axis.
///
/// @param source source descriptor, which must not be mosaiced and must have square pixels.
/// @param request clockwise angle and sampling policy.
/// @return the plan; `ErrorCode::IncompatibleArguments` for an unresolved mosaic or a pixel aspect
///         other than one; `ErrorCode::InvalidArgument` for an angle that is not finite, a canvas
///         rule outside its enumeration, or a sampling policy `planResize` would refuse; or
///         `ErrorCode::OutOfRange` for an expanded extent that does not fit 32 bits or lies beyond
///         `defaultEngineLimits()`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryPlan> planRotation(const ImageDescriptor& source,
                                                                     const RotationRequest& request);

/// Builds a destination-to-source affine warp plan.
///
/// @param source source descriptor, which must not be mosaiced.
/// @param request destination description fields, coefficients and sampling policy.
/// @return the plan; `ErrorCode::IncompatibleArguments` for an unresolved mosaic;
///         `ErrorCode::InvalidArgument` for a coefficient that is not finite, a destination pixel
///         aspect that is not finite and above zero, or a sampling policy `planResize` would refuse;
///         or `ErrorCode::OutOfRange` for a destination extent that is empty or beyond
///         `defaultEngineLimits()`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryPlan> planAffineWarp(const ImageDescriptor& source,
                                                                       const AffineWarpRequest& request);

/// Builds a destination-to-source projective warp plan.
///
/// The homogeneous denominator must be finite and nonzero at every destination pixel center; that
/// is checked while planning so a rejected transform cannot leave a partial destination.
///
/// @param source source descriptor, which must not be mosaiced.
/// @param request destination description fields, coefficients and sampling policy.
/// @return the plan; `ErrorCode::IncompatibleArguments` for an unresolved mosaic;
///         `ErrorCode::InvalidArgument` for a coefficient that is not finite, a destination pixel
///         aspect that is not finite and above zero, a sampling policy `planResize` would refuse, or
///         a denominator that is zero or not finite at some destination pixel center; or
///         `ErrorCode::OutOfRange` for a destination extent that is empty or beyond
///         `defaultEngineLimits()`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryPlan> planProjectiveWarp(const ImageDescriptor& source,
                                                                           const ProjectiveWarpRequest& request);

/// Builds an exact right-angle turn or mirror.
///
/// Every destination sample is one source sample copied bit for bit, so nothing is interpolated and a
/// mosaic is accepted: its filter array is restated at the destination - the named pattern of a
/// two-by-two array, or the six-by-six table of a tabulated one, each with its phase folded in and a
/// zero phase stated - so every sample stays under the filter it was measured through, whatever the
/// picture's dimensions. The four axis-swapping orientations swap the extent and invert the pixel
/// aspect. The coordinate origin is left as the source states it: this moves the picture, and a caller
/// re-expressing a bottom-up image top-down mirrors it vertically and states the other origin itself.
///
/// Every one of the eight is applied here, and only when a caller asks for it.
///
/// @param source source descriptor.
/// @param orientation the transform to apply.
/// @return the plan; `ErrorCode::InvalidArgument` for an orientation outside the enumeration; or
///         `ErrorCode::OutOfRange` when a swapped extent is beyond `defaultEngineLimits()`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryPlan> planOrientation(const ImageDescriptor& source,
                                                                        ImageOrientation orientation);

/// Counts and execution facts from one geometry transform.
struct GeometryResult
{
    /// Destination samples considered and written, summed over channels: the selection's total is
    /// every destination sample of every channel, and its valid count and the outputs written are
    /// the samples the operation wrote.
    KernelRunResult run{};

    /// Channels transformed.
    std::uint32_t channelCount = 0u;

    /// Workers the execution could use.
    std::uint32_t plannedWorkers = 0u;

    /// Context-owned coordinate, table and working-plane memory held at once, in bytes.
    std::uint64_t workingBytes = 0u;

    /// Whether the path that ran copied or selected stored values rather than computing them.
    ///
    /// True for a crop, an orientation, an integer enlargement, a minimum or maximum reduction, a
    /// `NearestExact` resize that substituted nothing, and a nearest resize of all-valid
    /// single-sample planes under the clamp border; false otherwise, even where a computed value
    /// happens to equal a stored one.
    bool exactStoredValues = false;
};

/// Executes a crop, resize, integer resample, rotation, warp or orientation plan over every channel.
///
/// Source and destination must be disjoint and their layouts must agree with the plan's source and
/// destination descriptors. `destinationValidity` is required and is rewritten completely: a set
/// bit means the operation wrote that sample, and every padding bit remains clear. With one shared
/// plane a bit is set only when every channel wrote its sample there. A destination sample the
/// operation did not write keeps whatever it held. Validation of layouts, policies, all projective
/// coordinates and the output mask completes before the first sample write.
///
/// Thread-safe with respect to `execution`, the source, the plan and the selection, which are only
/// read. Runs on the threads the operation was admitted and checks cancellation at row-chunk
/// boundaries; cancellation after execution starts may leave complete row chunks written and the
/// mask not yet describing them.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType resident representation used by both images and both descriptors.
/// @param source first byte of the source image.
/// @param sourceLayout where source samples sit.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where destination samples sit.
/// @param plan a checked plan whose source and destination descriptors match the call.
/// @param selection source validity rule and optional packed bits.
/// @param destinationValidity packed output bits, one shared plane or one per channel.
/// @return counts and execution facts; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one;
///         `ErrorCode::InvalidArgument` for an unknown representation, a null buffer, a plan whose
///         descriptors state another representation, a buffer not aligned to its sample, an
///         explicit-mask rule without its mask or a mask given to a rule that reads none, a plan
///         whose operation, matrix or sampling policy is unusable, or a transformed coordinate that
///         is not finite; `ErrorCode::IncompatibleArguments` for a layout whose element size or
///         extent disagrees with the plan, source and destination storage that overlap, a source or
///         output mask that does not cover its image, or a plan that is not the one its builder
///         produces; any refusal of that builder; `ErrorCode::LimitExceeded` for a working set whose
///         size overflows; or the engine context's refusal of the working memory.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GeometryResult>
transformImageGeometry(const ExecutionContext& execution, SampleType sampleType, const void* source,
                       const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                       const GeometryPlan& plan, const GeometrySelection& selection,
                       const ValidityMaskView<std::uint8_t>& destinationValidity);

/// Geometry operations state a new descriptor, invalidate astrometry, drop source codec cards and
/// withdraw an output intent. Their explicit output mask is the regenerated validity claim.
inline constexpr OperationEffect kGeometryEffect{
    .description = DescriptionEffect::Restates,
    .solution = SolutionEffect::Invalidates,
    .codecMetadata = CodecMetadataEffect::Drops,
    .outputIntent = OutputIntentEffect::Withdraws,
    .validity = {.policy = ValidityPolicy::Regenerate,
                 .produced = *ValidityDescriptor::create({.rule = ValidityRule::ExplicitMask})}};

} // namespace an
