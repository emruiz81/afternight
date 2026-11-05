// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Describing a registration transform as an OpenCV matrix, without copying it.
///
/// A `cv::Mat` over a transform is what OpenCV's own warp and point-transform calls take, and building
/// one needs neither an allocation nor a copy: a checked transform already holds its coefficients as a
/// row-major three-by-three block of doubles, and both shapes OpenCV wants are contiguous views of
/// that block - the affine shape is its first two rows at the same row step. So this describes, and
/// OpenCV reads the engine's own storage.
///
/// It is a separate header from the transform itself on purpose. A consumer that never speaks to
/// OpenCV never acquires the adapter's declarations, which is the same rule
/// `Interop/OpenCvAdapter.h` follows for image samples.
///
/// The description borrows. It points into the transform it was built from and owns nothing, so it
/// must not outlive it - and because a transform is immutable and trivially copyable, the way to
/// keep a description alive is to keep the transform alive beside it. OpenCV must only read it:
/// writing through the matrix would change the transform without the checks
/// `RegistrationTransform::create` makes.
///
/// **The bridge describes the coefficients; it does not convert their pixel convention.** The
/// matrix maps source to reference in engine pixel coordinates, whose first pixel centre is
/// (0.5, 0.5). `cv::transform` and `cv::perspectiveTransform` applied to engine positions therefore
/// answer in engine positions. `cv::warpAffine` and `cv::warpPerspective` put pixel centres at
/// integers instead, so a warp through this description is not the warp `warpRegisteredImage`
/// performs, which shifts by the half pixel on both sides of the matrix before its sampling
/// kernels see it: for an affine or similarity transform with linear part `A`, the OpenCV result
/// lands `(A - I)(0.5, 0.5)` away, which is zero only for a pure translation, and for a projective
/// one the offset varies across the frame. A caller resampling through OpenCV makes that
/// correction itself.

#pragma once

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Interop/OpenCvAdapter.h>

#include <cstdint>
#include <optional>

namespace an
{

/// Which of OpenCV's two transform shapes a description is asked for.
enum class RegistrationMatrixShape : std::uint8_t
{
    /// The two-by-three shape `cv::warpAffine` and `cv::transform` take.
    ///
    /// Carries the linear part and the translation and nothing else, so it describes a similarity
    /// or an affine map completely and a projective one not at all.
    AffineTwoByThree = 0u,

    /// The three-by-three shape `cv::warpPerspective` and `cv::perspectiveTransform` take.
    ///
    /// Describes every linear kind, including the two whose last row is `[0, 0, 1]`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    ProjectiveThreeByThree
};

/// Whether a value names a matrix shape this bridge describes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param shape the value to test.
/// @return true for both enumerators.
[[nodiscard]] constexpr bool isRegistrationMatrixShape(RegistrationMatrixShape shape) noexcept
{
    return static_cast<std::uint8_t>(shape) <=
           static_cast<std::uint8_t>(RegistrationMatrixShape::ProjectiveThreeByThree);
}

/// The shape that describes a transform of this kind completely and no more.
///
/// Stated here so a caller can ask for it by name and so a caller with a reason to want the larger
/// shape says so.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the transform's kind.
/// @return the projective shape for a projective kind and the affine shape for every other. The
///         polynomial kinds get the affine shape too, although `checkRegistrationOpenCvMatrix`
///         refuses a polynomial transform in either shape and a polynomial kind does not say
///         whether its linear part is projective.
[[nodiscard]] constexpr RegistrationMatrixShape
naturalRegistrationMatrixShape(const RegistrationTransformKind kind) noexcept
{
    return kind == RegistrationTransformKind::Projective ? RegistrationMatrixShape::ProjectiveThreeByThree
                                                         : RegistrationMatrixShape::AffineTwoByThree;
}

/// How many rows a shape has.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param shape the shape.
/// @return two or three, or zero for a value outside the enumeration.
[[nodiscard]] constexpr std::int32_t registrationMatrixRowCount(const RegistrationMatrixShape shape) noexcept
{
    switch (shape)
    {
    case RegistrationMatrixShape::AffineTwoByThree:
        return 2;
    case RegistrationMatrixShape::ProjectiveThreeByThree:
        return 3;
    }
    return 0;
}

/// Whether this transform can be described as a matrix of this shape, without losing part of it.
///
/// Two ways it cannot. A transform carrying a polynomial is a matrix followed by a correction, and
/// a matrix alone describes the correction not at all; and a projective transform asked for the
/// affine shape would leave its perspective row behind. Both are
/// `BridgeRefusal::ArrangementNotRepresentable`: the value's arrangement, not its numbers, is what
/// the requested matrix cannot hold.
///
/// A caller that wants the linear part of a polynomial transform on purpose asks the transform for
/// its `matrix()` and says so at that call site, rather than getting it from a bridge that looks
/// like it described the whole thing.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param transform the transform to describe.
/// @param shape the shape asked for.
/// @param typeLayout the packing the caller's OpenCV uses.
/// @return `BridgeRefusal::None` when the matrix can be described;
///         `BridgeRefusal::ArrangementNotRepresentable` for an unknown shape, an unusable packing,
///         a polynomial transform, or a projective transform in the affine shape; or
///         `BridgeRefusal::SampleTypeNotRepresentable` when the caller's OpenCV has no
///         double-precision depth at this engine's value for it.
[[nodiscard]] constexpr BridgeRefusal checkRegistrationOpenCvMatrix(const RegistrationTransform& transform,
                                                                    const RegistrationMatrixShape shape,
                                                                    const OpenCvTypeLayout& typeLayout) noexcept
{
    if (!isRegistrationMatrixShape(shape) || !isOpenCvTypeLayout(typeLayout))
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (!transform.isLinear())
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (shape == RegistrationMatrixShape::AffineTwoByThree &&
        transform.linearKind() == RegistrationTransformKind::Projective)
    {
        return BridgeRefusal::ArrangementNotRepresentable;
    }
    if (!openCvTypeCode(OpenCvDepth::Float64, 1, typeLayout).has_value())
    {
        return BridgeRefusal::SampleTypeNotRepresentable;
    }
    return BridgeRefusal::None;
}

/// The matrix description for a transform, when the bridge holds.
///
/// The caller builds `cv::Mat(description.rows, description.cols, description.type,
/// description.data, description.stepBytes)` from what comes back, and every argument of that
/// constructor has been checked. The data pointer is the transform's own coefficient block, so the
/// matrix reads the engine's storage and no copy happens at all.
///
/// The row step is three doubles whatever the shape, because both shapes are views of the same
/// row-major block. That is what makes the affine shape free rather than a repack.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param transform the transform to describe. It must outlive the description.
/// @param shape the shape asked for.
/// @param typeLayout the packing the caller's OpenCV uses.
/// @return the description, or an empty optional when `checkRegistrationOpenCvMatrix` refuses.
[[nodiscard]] constexpr std::optional<OpenCvMatDescription>
describeRegistrationOpenCvMatrix(const RegistrationTransform& transform, const RegistrationMatrixShape shape,
                                 const OpenCvTypeLayout& typeLayout) noexcept
{
    if (checkRegistrationOpenCvMatrix(transform, shape, typeLayout) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    const std::optional<std::int32_t> code = openCvTypeCode(OpenCvDepth::Float64, 1, typeLayout);
    if (!code.has_value())
    {
        return std::nullopt;
    }
    return OpenCvMatDescription{.data = const_cast<double*>(transform.matrix().data()),
                                .rows = registrationMatrixRowCount(shape),
                                .cols = 3,
                                .type = *code,
                                .stepBytes = 3u * sizeof(double)};
}

} // namespace an
