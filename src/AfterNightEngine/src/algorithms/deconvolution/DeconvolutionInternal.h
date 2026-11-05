// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the convolution both deconvolution methods are built out of.
//
// Richardson-Lucy blurs its estimate and its ratio once per iteration, the Wiener filter divides one
// spectrum by another, and the edge taper blurs the frame once per pass. All four are the same two
// operations - a weighted sum over a kernel footprint, or a multiplication of two spectra - and this
// is where they live so that the two methods cannot disagree about a border, a normalisation or a
// transform size.
//
// The engine is built once per operation and holds everything the repeated convolutions need: the
// border rule resolved into row and column index tables; the normalised kernel and its mirror on the
// direct path; or, on the transform path, the spectra the operation asked for, one spatial and one
// spectral work buffer and the two transform plans - with the direct kernels beside them when a
// Wiener filter's taper convolves directly. Nothing is allocated per iteration, and because the
// work buffers are shared, one engine runs one convolution at a time.

#pragma once

#include <AfterNightEngine/Algorithms/Deconvolution/Deconvolution.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "interop/fftw/FftwRealTransform.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <cstdint>
#include <span>

/// The deconvolution domain's private machinery: the working plane, the convolution engine both
/// methods share, the flux reduction, the edge taper and the domain's failure builders.
namespace an::deconvolution_detail
{

/// A tightly packed Float32 plane the working set is made of.
///
/// Trivially copyable; it points into memory it does not own.
struct WorkingPlane
{
    /// The first sample. Never null in a plane that describes anything.
    float* base = nullptr;

    /// How many pixels one row holds.
    std::uint32_t width = 0u;

    /// How many rows the plane holds.
    std::uint32_t height = 0u;

    /// How many samples the plane holds.
    ///
    /// @return width times height.
    [[nodiscard]] constexpr std::uint64_t sampleCount() const noexcept
    {
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    }
};

/// One coordinate resolved against an extent under a stated border rule.
///
/// @param position where the read wants to be, possibly outside.
/// @param extent how many samples there are. Must be at least one.
/// @param border which rule answers a coordinate outside. Must resolve one.
/// @return the sample index inside the extent.
[[nodiscard]] std::uint32_t resolveCoordinate(std::int64_t position, std::uint32_t extent,
                                              KernelBorderMode border) noexcept;

/// Whether a transform of this size is one the library factorises quickly.
///
/// FFTW's own cost is lowest when the length factors into small primes, and the cost of padding out
/// to the next such length is far below the cost of a length with a large prime factor.
///
/// @param value the length.
/// @return true when the length factors into twos, threes and fives alone.
[[nodiscard]] bool isFastTransformLength(std::uint64_t value) noexcept;

/// The smallest fast transform length at or above a value.
///
/// @param value the length that must be covered.
/// @return the length to transform at.
[[nodiscard]] std::uint64_t nextFastTransformLength(std::uint64_t value) noexcept;

/// Which of the three kernel spectra an operation will actually use, and whether it also needs the
/// direct kernel beside them.
///
/// Building a spectrum costs a transform of the whole padded frame, so a Wiener filter that only
/// divides by the kernel must not pay for the two a Richardson-Lucy run needs. The flags are stated
/// by the caller rather than derived, because whether the taper runs decides two of them. The three
/// spectrum flags are read only on the transform path; the direct path always builds both direct
/// kernels.
struct ConvolutionEngineNeeds
{
    /// Whether the engine will be asked to convolve by the kernel through the transform.
    bool convolution = true;

    /// Whether it will be asked to convolve by the mirrored kernel, which is the adjoint operator,
    /// through the transform.
    bool adjoint = true;

    /// Whether it will be asked to divide by the kernel's spectrum, which `wiener` requires.
    bool inverseFilter = false;

    /// Whether it also needs the direct spatial kernel while its primary path is a transform.
    ///
    /// A Wiener solve is necessarily spectral, but its edge taper still honours a caller who asks
    /// for direct convolution. Both forms then live in the one operation engine.
    bool directConvolution = false;
};

/// The convolution one deconvolution runs, in whichever form it decided on.
///
/// Move-only, because it owns context memory and two transform plans. Not reentrant: `convolve` and
/// `wiener` are `const` but write the engine's one spatial and one spectral work buffer on the
/// transform path, so calls on one engine must not overlap. Each call parallelises inside itself.
class ConvolutionEngine
{
public:
    /// An engine holding nothing.
    ConvolutionEngine() noexcept = default;

    ConvolutionEngine(const ConvolutionEngine&) = delete;
    ConvolutionEngine& operator=(const ConvolutionEngine&) = delete;

    /// Takes over another engine, leaving it holding nothing.
    ///
    /// @param other the engine to take over.
    ConvolutionEngine(ConvolutionEngine&& other) noexcept = default;

    /// Releases anything held and takes over another engine.
    ///
    /// @param other the engine to take over.
    /// @return this engine.
    ConvolutionEngine& operator=(ConvolutionEngine&& other) noexcept = default;

    /// Builds the convolution for one operation.
    ///
    /// Normalises the kernel to unit sum into the engine's own memory, so the caller's plane is
    /// never written; resolves the border into the index tables; and on the transform path pads to
    /// the next length whose only prime factors are two, three and five, plans both transforms and
    /// computes the spectra `needs` asks for once.
    ///
    /// @param execution the operation policy and engine-context lease.
    /// @param psf the caller's point spread function, which must already have passed `checkPsf`.
    /// @param useTransform whether to take the transform path.
    /// @param width the image width the engine will convolve.
    /// @param height the image height.
    /// @param border how a coordinate outside the image is answered.
    /// @param needs which of the three kernel spectra the operation will use.
    /// @param operation the name carried by any failure.
    /// @return the engine; the memory resource's refusal; `ErrorCode::LimitExceeded` for a padded
    ///         transform wider or taller than the library can plan; the transform adapter's refusal,
    ///         which is `ErrorCode::ExternalFailure` when the library declines a plan; or
    ///         `ErrorCode::Internal` when a buffer does not describe a plane the adapter accepts.
    [[nodiscard]] static Result<ConvolutionEngine> create(const ExecutionContext& execution, PlaneView<const float> psf,
                                                          bool useTransform, std::uint32_t width, std::uint32_t height,
                                                          KernelBorderMode border, const ConvolutionEngineNeeds& needs,
                                                          const char* operation);

    /// Whether this engine transforms rather than walking the footprint.
    ///
    /// @return true on the transform path.
    [[nodiscard]] bool usesTransform() const noexcept
    {
        return m_usesTransform;
    }

    /// The transform's width, in samples, or zero on the direct path.
    ///
    /// @return the width.
    [[nodiscard]] std::uint32_t transformWidth() const noexcept
    {
        return m_transformWidth;
    }

    /// The transform's height, in samples, or zero on the direct path.
    ///
    /// @return the height.
    [[nodiscard]] std::uint32_t transformHeight() const noexcept
    {
        return m_transformHeight;
    }

    /// How many bytes of context memory this engine holds.
    ///
    /// @return the byte count.
    [[nodiscard]] std::uint64_t workingBytes() const noexcept
    {
        return m_memory.bytes();
    }

    /// Convolves one plane by the point spread function, or by its mirror.
    ///
    /// @param execution the operation policy and engine-context lease.
    /// @param source the plane to read.
    /// @param destination where the answer goes, of the source's extent and not the source.
    /// @param mirrored whether to convolve by the mirrored kernel, which is the adjoint operator.
    /// @param useTransform whether this call uses the transform or the direct kernel.
    /// @param operation the name carried by any failure.
    /// @return success; an execution, cancellation or transform-library failure; or
    ///         `ErrorCode::InvalidState` when the engine was not built with the form or the spectrum
    ///         this call asks for.
    [[nodiscard]] Status convolve(const ExecutionContext& execution, const WorkingPlane& source,
                                  const WorkingPlane& destination, bool mirrored, bool useTransform,
                                  const char* operation) const;

    /// Divides one plane's spectrum by the point spread function's, held away from zero.
    ///
    /// Each frequency becomes the observed one times the conjugate of the kernel's wrapped response,
    /// over that response's squared magnitude plus `regularization`, all in Float32. A non-finite
    /// answer sample is stored as zero. Only available on the transform path, which is the only form
    /// a Wiener filter has, and only on an engine built with `ConvolutionEngineNeeds::inverseFilter`;
    /// a missing inverse-filter spectrum returns `ErrorCode::InvalidState` before any transform.
    ///
    /// @param execution the operation policy and engine-context lease.
    /// @param source the observed plane.
    /// @param destination where the restored plane goes.
    /// @param regularization the assumed noise-to-signal power ratio.
    /// @param nonNegative whether the result is held at or above zero.
    /// @param operation the name carried by any failure.
    /// @return success, or an execution, cancellation, state or transform-library failure.
    [[nodiscard]] Status wiener(const ExecutionContext& execution, const WorkingPlane& source,
                                const WorkingPlane& destination, double regularization, bool nonNegative,
                                const char* operation) const;

private:
    /// Builds an engine over its own memory.
    ///
    /// @param memory the block every buffer below points into.
    ConvolutionEngine(plane_detail::ScopedContextMemory&& memory) noexcept : m_memory(std::move(memory)) {}

    /// One output sample whose window reaches outside the frame.
    ///
    /// The border-resolving half of the direct convolution, kept out of the interior loop so the
    /// interior is two contiguous runs and nothing else.
    ///
    /// @param source the plane being read.
    /// @param walked the kernel, in the order the walk reads it.
    /// @param x the output column.
    /// @param y the output row.
    /// @return the convolved sample.
    [[nodiscard]] float borderedSample(const WorkingPlane& source, std::span<const float> walked, std::uint32_t x,
                                       std::uint64_t y) const noexcept;

    /// Fills the transform's real buffer with the source extended by one kernel radius on each side
    /// under the engine's border rule, and zeros from there out to the padded extent.
    ///
    /// @param execution the operation policy and engine-context lease.
    /// @param source the plane to read.
    /// @param buffer the real buffer to fill.
    /// @return success, or an execution or cancellation failure.
    [[nodiscard]] Status fillTransformBuffer(const ExecutionContext& execution, const WorkingPlane& source,
                                             std::span<float> buffer) const;

    /// The block every buffer below points into.
    plane_detail::ScopedContextMemory m_memory{};

    /// The normalised kernel, on the direct path.
    std::span<float> m_kernel{};

    /// The mirrored normalised kernel, on the direct path.
    std::span<float> m_mirrored{};

    /// The kernel's spectrum, on the transform path.
    std::span<float> m_kernelSpectrum{};

    /// The mirrored kernel's spectrum, on the transform path.
    std::span<float> m_mirroredSpectrum{};

    /// The kernel's circularly wrapped spectrum, which is what a Wiener filter divides by.
    std::span<float> m_wrappedSpectrum{};

    /// The real buffer one convolution transforms from and back into, on the transform path.
    ///
    /// The transform is out of place, which is the faster of the two forms the library offers: an
    /// in-place real transform works inside a padded row layout, and on the frames this engine
    /// transforms that costs about twice what the separated form does. The extra buffer is what an
    /// operation holding a spectrum per kernel was going to pay for anyway.
    std::span<float> m_spatial{};

    /// The half-spectrum one convolution multiplies in, on the transform path.
    std::span<float> m_work{};

    /// Where each row a convolution reads sits, for every offset from one kernel radius before the
    /// first row to one after the last.
    ///
    /// The border rule is a `switch` and a loop, and putting either inside the innermost loop of a
    /// convolution costs a branch per kernel tap on every sample of the frame. It depends only on
    /// the geometry, so it is resolved once here instead.
    std::span<const std::uint32_t> m_rowIndices{};

    /// Where each column a convolution reads sits, on the same convention.
    std::span<const std::uint32_t> m_columnIndices{};

    /// The forward transform plan.
    fftw_detail::FftwRealTransformPlan m_forward{};

    /// The inverse transform plan.
    fftw_detail::FftwRealTransformPlan m_inverse{};

    /// The image width this engine convolves.
    std::uint32_t m_width = 0u;

    /// The image height.
    std::uint32_t m_height = 0u;

    /// The kernel's width.
    std::uint32_t m_kernelWidth = 0u;

    /// The kernel's height.
    std::uint32_t m_kernelHeight = 0u;

    /// The transform's width, or zero on the direct path.
    std::uint32_t m_transformWidth = 0u;

    /// The transform's height, or zero on the direct path.
    std::uint32_t m_transformHeight = 0u;

    /// How many reals one row of a half-spectrum holds, which is twice its complex element count.
    std::uint64_t m_rowReals = 0u;

    /// How a coordinate outside the image is answered.
    KernelBorderMode m_border = KernelBorderMode::ReflectExcludingEdge;

    /// Whether this engine transforms rather than walking the footprint.
    bool m_usesTransform = false;
};

/// The sum of a plane's finite positive samples, in Float64.
///
/// The one reduction in this domain that needs a wider accumulator than the samples: a frame's total
/// flux is a sum of tens of millions of Float32 values, and the error of a Float32 running total over
/// that many terms is what the flux correction would then be correcting.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the plane to sum.
/// @return the sum, or an execution or cancellation failure.
[[nodiscard]] Result<double> positiveSum(const ExecutionContext& execution, const WorkingPlane& plane);

/// Softens a frame's edges so the blur model holds there.
///
/// A deconvolution assumes the recorded frame is a blur of something, which at the edge is false:
/// the blur mixed in light from outside the frame that was never recorded. Each pass blurs the
/// current frame and blends each sample between the frame and its blur: the frame's weight is one
/// from `radius` pixels in and falls as a squared sine to zero at the edge itself, where the blur
/// alone remains. The whole result is then rescaled to the observation's positive flux. With a
/// radius that is not positive or no passes, nothing is written and the caller reads the
/// observation itself.
///
/// @param execution the operation policy and engine-context lease.
/// @param observed the frame to taper.
/// @param tapered where the tapered frame goes.
/// @param scratchA a plane of the frame's extent, which receives each pass's blur.
/// @param scratchB a plane of the frame's extent, which alternates with `tapered` between passes.
/// @param engine the convolution to blur with.
/// @param useTransform whether the taper uses the transform or the direct kernel.
/// @param radius how far the taper reaches in from each edge, in pixels.
/// @param iterations how many passes to run.
/// @param operation the name carried by any failure.
/// @return success, or an execution, cancellation or transform-library failure.
[[nodiscard]] Status applyEdgeTaper(const ExecutionContext& execution, const WorkingPlane& observed,
                                    const WorkingPlane& tapered, const WorkingPlane& scratchA,
                                    const WorkingPlane& scratchB, const ConvolutionEngine& engine, bool useTransform,
                                    double radius, std::uint32_t iterations, const char* operation);

/// One failure from the deconvolution domain.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status deconvolutionFailure(ErrorCode code, const char* operation, const char* message);

/// One failure carrying what was asked for and what was allowed.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @param requested the number the caller supplied.
/// @param limit the number the contract allows.
/// @return the failure, carrying both counts.
[[nodiscard]] Status deconvolutionLimitFailure(ErrorCode code, const char* operation, const char* message,
                                               std::uint64_t requested, std::uint64_t limit);

} // namespace an::deconvolution_detail
