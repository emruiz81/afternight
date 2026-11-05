// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Blurring an image with a Gaussian, exactly or by repeated box passes.
///
/// A Gaussian blur is the neighbourhood filter the rest of the engine leans on: a background model,
/// a star profile, an unsharp mask, a wavelet scale and a deconvolution point-spread function all
/// reach for one. It is a separable convolution and could be spelled as two tap arrays through
/// `Algorithms/Spatial/Convolution.h`. It is here as an operation of its own because everything a
/// caller gets wrong about a Gaussian is in the part that turns a sigma into taps: how far out the
/// kernel is truncated, whether the taps are sampled or integrated, and whether what remains is
/// renormalized. Those decisions are stated once here rather than repeated at every call site.
///
/// **A tap is the density at the sample's centre, and the array is renormalized after truncation.**
/// The alternative - integrating the density across each sample's width - differs by less than a
/// part in ten thousand at the sigmas an astronomical image uses and costs an error function per
/// tap. Renormalizing after truncation is what makes the filter preserve a flat surface exactly:
/// without it a truncated Gaussian sums to slightly less than one and every blur darkens the image
/// by an amount that depends on how far it was truncated.
///
/// **Truncation is a number the caller can read, not a constant inside an implementation.** The
/// default is four sigma, which leaves less than one part in fifteen thousand of the density
/// outside the kernel. The radius that follows from it is reported by the plan, so the halo a tiled
/// caller has to allow for is knowable before the blur runs.
///
/// **Production and real-time preview are different entry points.** `planGaussianBlur` and
/// `blurImageGaussian` always run the Gaussian itself: two sampled-tap passes whose cost grows with
/// the radius. `planRealtimePreviewGaussianBlur` and `blurImageRealtimePreviewGaussian` run a fast
/// approximation for an interactive control: three running-sum box passes along each axis, whose
/// cost does not depend on the radius. The approximation is a different picture, not a faster route
/// to the same one, so the production entry points refuse a repeated-box plan and the preview entry
/// points refuse an exact plan. A processed image therefore cannot acquire the approximation through
/// a method flag. Both are kept on measurement: the exact blur's cost grows with sigma and the
/// approximation's does not, so from a sigma of about two and a half the approximation is faster -
/// five or six times at ten - while it stays within one per cent of the Gaussian.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <span>

namespace an
{

/// How a Gaussian blur is produced.
enum class GaussianMethod : std::uint8_t
{
    /// The Gaussian itself, as two passes of sampled and renormalized taps.
    ///
    /// The cost per pixel grows with the radius and therefore with the sigma. What every accuracy
    /// argument in the engine is written against.
    SeparableTaps = 0u,

    /// Three box averages along each axis, each computed as a running sum for real-time preview.
    ///
    /// The cost per pixel does not depend on the radius. It is an approximation: the plan reports
    /// the sigma the chosen box widths realize, and the picture differs from the Gaussian by a few
    /// parts in a thousand of full scale near an edge.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RepeatedBox = 1u
};

/// Whether a value names a method this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the value to inspect.
/// @return true for every implemented method.
[[nodiscard]] constexpr bool isGaussianMethod(GaussianMethod method) noexcept
{
    return method == GaussianMethod::SeparableTaps || method == GaussianMethod::RepeatedBox;
}

/// The method's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param method the method to name. A value outside the enumeration is named
///        `"UnknownGaussianMethod"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* gaussianMethodName(GaussianMethod method) noexcept;

/// How many box averages `GaussianMethod::RepeatedBox` applies along each axis.
///
/// Three, which is where the approximation stops improving enough to pay for another pass: one box
/// is visibly square, two are triangular, three are within a few parts in a thousand of the
/// Gaussian, and a fourth changes the picture less than the rounding of an eight-bit sample.
inline constexpr std::uint32_t kGaussianBoxPasses = 3u;

/// How many sigmas of the density a plan keeps when the caller states no truncation.
///
/// Four, which leaves 6.3e-5 of the density outside the kernel - below the quantum of a
/// fourteen-bit sample and comfortably below the noise of any real frame.
inline constexpr double kDefaultGaussianTruncation = 4.0;

/// The narrowest sigma a plan will build a kernel for.
///
/// Below this the kernel is one tap and the blur is the identity, which is a result a caller almost
/// never means to ask for. A sigma at or below this is refused rather than silently doing nothing.
inline constexpr double kMinimumGaussianSigma = 1.0e-3;

/// What a caller asks a Gaussian blur for.
///
/// The two sigmas are separate and both default to one: an anisotropic blur is ordinary - a
/// spectrum is blurred along its dispersion axis and not across it - and a caller wanting an
/// isotropic blur states the same number twice rather than relying on one field to shadow another.
struct GaussianRequest
{
    /// Standard deviation along a row, in pixels. Must be finite and above `kMinimumGaussianSigma`.
    double sigmaX = 1.0;

    /// Standard deviation down a column, in pixels. Must be finite and above
    /// `kMinimumGaussianSigma`.
    double sigmaY = 1.0;

    /// How many sigmas of the density the kernel keeps. Must be finite and at least one.
    ///
    /// Read by `GaussianMethod::SeparableTaps` alone: a box width is derived from the sigma it has
    /// to realize rather than from where the density is cut off.
    double truncation = kDefaultGaussianTruncation;

    /// Which implementation the plan records.
    ///
    /// Production callers leave this at `SeparableTaps`; `planGaussianBlur` refuses any other
    /// value. The dedicated `RealtimePreviewGaussianRequest` has no method field and its named
    /// builder records `RepeatedBox`, so the approximation cannot leak through an ordinary
    /// production request.
    GaussianMethod method = GaussianMethod::SeparableTaps;

    /// How a neighbour outside the image and a neighbour the validity rule rejected are supplied.
    ///
    /// The default is `KernelBorderMode::ReflectExcludingEdge` rather than the kernel family's
    /// `Clamp`, because clamping biases a blur near an edge towards the edge sample while
    /// reflecting about it leaves a linear ramp linear across the border.
    StencilBorderPolicy borders{.border = KernelBorderMode::ReflectExcludingEdge};

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// What an interactive control asks the approximate Gaussian for.
///
/// This deliberately has no method or truncation: the named builder always chooses three repeated
/// boxes, and a box width follows from sigma rather than Gaussian-density truncation.
/// Results are disposable display previews only; they must not be saved or fed to production
/// processing.
struct RealtimePreviewGaussianRequest
{
    /// Standard deviation along a row, in pixels. Must be finite and above `kMinimumGaussianSigma`.
    double sigmaX = 1.0;

    /// Standard deviation down a column, in pixels. Must be finite and above
    /// `kMinimumGaussianSigma`.
    double sigmaY = 1.0;

    /// How an outside or invalid neighbour is supplied.
    StencilBorderPolicy borders{.border = KernelBorderMode::ReflectExcludingEdge};

    /// What the disposable preview promises and how it is written back.
    SpatialOutputPolicy output{};
};

/// A validated Gaussian blur: what was asked for, and what will actually run.
///
/// Trivially copyable and allocation-free. It holds the radii and the box widths rather than the
/// taps, because a tap array is up to `kMaxStencilTaps` doubles per axis and a plan a caller keeps
/// beside a filter should not be four kilobytes. `writeGaussianTaps` produces the coefficients for
/// a caller that wants to see or reuse them; the execution produces them itself, on the stack,
/// once per call.
class GaussianPlan
{
public:
    /// Records a request and derives everything that follows from it.
    ///
    /// Public for the reason `ColorConversionPlan`'s constructor is: the free builder below is
    /// where the rules live, and this only derives what a checked request implies. A request that
    /// has not been through `planGaussianBlur` may produce a plan whose numbers are meaningless;
    /// `blurImageGaussian` checks them again before it reads a sample.
    ///
    /// @param request the sigmas, truncation, method, border and output policy.
    AFTERNIGHTENGINE_API explicit GaussianPlan(const GaussianRequest& request) noexcept;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const GaussianRequest& request() const noexcept;

    /// Which method will run.
    ///
    /// @return the request's method.
    [[nodiscard]] AFTERNIGHTENGINE_API GaussianMethod method() const noexcept;

    /// Half the horizontal extent the filter reads, in pixels.
    ///
    /// For `SeparableTaps` it is the truncated kernel's radius; for `RepeatedBox` it is the sum of
    /// the three box radii, which is how far a sample can travel through three passes.
    ///
    /// @return the radius, never above `kMaxStencilRadius` for a plan `planGaussianBlur` accepted.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t horizontalRadius() const noexcept;

    /// Half the vertical extent the filter reads, in pixels.
    ///
    /// @return the radius, never above `kMaxStencilRadius` for a plan `planGaussianBlur` accepted.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t verticalRadius() const noexcept;

    /// The three box radii applied along a row, in order.
    ///
    /// @return the radii, borrowed from this plan. All three are zero under `SeparableTaps`, which
    ///         runs no box pass.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::uint32_t> horizontalBoxRadii() const noexcept;

    /// The three box radii applied down a column, in order.
    ///
    /// @return the radii, borrowed from this plan.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::uint32_t> verticalBoxRadii() const noexcept;

    /// The horizontal sigma the plan actually realizes.
    ///
    /// Equal to `request().sigmaX` under `SeparableTaps`. Under `RepeatedBox` it is the standard
    /// deviation of the three chosen box widths convolved together, which is the nearest the
    /// integer widths can come to what was asked for.
    ///
    /// @return the realized sigma, in pixels.
    [[nodiscard]] AFTERNIGHTENGINE_API double realizedSigmaX() const noexcept;

    /// The vertical sigma the plan actually realizes.
    ///
    /// @return the realized sigma, in pixels.
    [[nodiscard]] AFTERNIGHTENGINE_API double realizedSigmaY() const noexcept;

    /// How many times the plan walks the whole image.
    ///
    /// @return two for `SeparableTaps`, and `2 * kGaussianBoxPasses` for `RepeatedBox`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// The request this plan records.
    GaussianRequest m_request{};

    /// Half the horizontal extent the filter reads.
    std::uint32_t m_horizontalRadius = 0u;

    /// Half the vertical extent the filter reads.
    std::uint32_t m_verticalRadius = 0u;

    /// The three horizontal box radii, all zero for the exact method.
    std::array<std::uint32_t, kGaussianBoxPasses> m_horizontalBoxRadii{};

    /// The three vertical box radii, all zero for the exact method.
    std::array<std::uint32_t, kGaussianBoxPasses> m_verticalBoxRadii{};

    /// The horizontal sigma the chosen widths realize.
    double m_realizedSigmaX = 0.0;

    /// The vertical sigma the chosen widths realize.
    double m_realizedSigmaY = 0.0;
};

/// Validates a request and derives the plan that would run it.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution, because deciding a
/// kernel is a question about numbers.
///
/// @param request the sigmas, truncation, border and output policy. Its method must be
///        `GaussianMethod::SeparableTaps`; repeated boxes belong to the real-time-preview builder.
/// @return the plan; `ErrorCode::IncompatibleArguments` for any method other than
///         `GaussianMethod::SeparableTaps`, a value outside the enumeration included;
///         `ErrorCode::InvalidArgument` for a non-finite sigma, a sigma at or below
///         `kMinimumGaussianSigma`, a non-finite truncation, a truncation below one, an unknown or
///         inapplicable border mode, a non-finite border or invalid-neighbour value, an output
///         contract or conversion field outside its enumeration, or a write-back policy naming
///         `DomainMapping::FullScale`; or `ErrorCode::OutOfRange` when the radius the sigma implies
///         is above `kMaxStencilRadius`, carrying the radius and the limit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GaussianPlan> planGaussianBlur(const GaussianRequest& request);

/// Validates and plans the repeated-box blur for an interactive preview.
///
/// This is the only builder that accepts the approximation. Its result is accepted only by
/// `blurImageRealtimePreviewGaussian`, so a production execution cannot accidentally consume it.
/// Thread-safe and allocation-free.
///
/// @param request preview sigmas, border and output policy.
/// @return a repeated-box plan; the same numeric, border, write-back and radius failures as
///         `planGaussianBlur`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GaussianPlan>
planRealtimePreviewGaussianBlur(const RealtimePreviewGaussianRequest& request);

/// Writes the sampled, renormalized taps of one axis.
///
/// The coefficients `GaussianMethod::SeparableTaps` applies, produced for a caller that wants to
/// inspect them, hand them to `convolveImageSeparable` itself, or compose them with another filter.
/// Tap `i` is the density at offset `i - radius` divided by the sum of all of them, so the array
/// sums to one to within Float64 rounding and the filter preserves a flat surface.
///
/// Thread-safe and allocation-free.
///
/// @param sigma the standard deviation, in pixels. Must be finite and above `kMinimumGaussianSigma`.
/// @param radius half the kernel width, in pixels. Must not exceed `kMaxStencilRadius`.
/// @param taps where the coefficients go, exactly `2 * radius + 1` of them.
/// @return how many taps were written; `ErrorCode::InvalidArgument` for a non-finite or too small
///         sigma; `ErrorCode::OutOfRange` for a radius above `kMaxStencilRadius`; or
///         `ErrorCode::IncompatibleArguments` when the array is not exactly `2 * radius + 1` long.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t> writeGaussianTaps(double sigma, std::uint32_t radius,
                                                                           std::span<double> taps);

/// Blurs every channel of an image with a Gaussian.
///
/// The untyped entry point, and the one the exported library carries. The arrangement, working
/// planes, validity, border and write-back rules are `convolveImageSeparable`'s, and the plan
/// decides which arithmetic runs between them.
///
/// One case runs a dedicated path, where the execution's CPU kernel table provides it - the AVX2 and
/// AVX-512 tables do and the scalar table does not: a planar, tightly packed Float32 image in which
/// every sample means something, under `KernelBorderMode::ReflectExcludingEdge` and the default
/// write-back, with both radii below half the extent. It narrows the taps to Float32, weights the
/// centre sample and each pair of samples at equal distance from it once, and keeps its sums in
/// Float32, so it is not bit-identical to the general path. It holds one Float32 intermediate plane,
/// and a memory-budget or allocation refusal of that plane falls back to the general path rather
/// than failing. Every other call is exactly `convolveImageSeparable` with the taps this header
/// defines, which is why the two are checkable against one another.
///
/// A repeated-box plan is refused and can be executed only by the explicitly named real-time-preview
/// entry point below.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at row and tile
/// boundaries, and is not failure-atomic.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the blurred samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan what to run, as `planGaussianBlur` accepted it.
/// @param selection which samples mean something.
/// @return the counts and the plan that ran; every failure `planGaussianBlur` and
///         `convolveImageSeparable` document.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
blurImageGaussian(const ExecutionContext& execution, SampleType sampleType, const void* source,
                  const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                  const GaussianPlan& plan, const SpatialSelection& selection = {});

/// Runs the retained repeated-box approximation for an interactive preview.
///
/// Each pass is a running sum over a moving window: the horizontal pass accumulates one sample and
/// drops one, while the vertical pass does the same over bands of columns. Its work per pixel is
/// independent of radius. The result differs from the exact Gaussian by a bounded amount and is
/// suitable only for a disposable real-time display preview. An exact plan is refused.
///
/// A planar, tightly packed Float32 image in which every sample means something, under
/// `KernelBorderMode::ReflectExcludingEdge` and the default write-back, with every box radius below
/// half the extent, runs a fused form: Float32 running sums in a fixed evaluation order, with the
/// three vertical passes chained through small row rings so their intermediate rows stay in cache.
/// Every other image runs the general passes, which accumulate in Float64 and apply the family's
/// border and validity rules.
///
/// Thread-safe with respect to `execution`, the source and the selection. Runs on the admitted
/// threads, checks cancellation at row and tile boundaries, and is not failure-atomic.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source preview samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the preview goes. Must not be null or overlap the source.
/// @param destinationLayout where it sits. Its extent must be the source's.
/// @param plan what to run, as `planRealtimePreviewGaussianBlur` accepted it.
/// @param selection which samples mean something.
/// @return the counts and plan that ran; `ErrorCode::IncompatibleArguments` for a plan that is not a
///         repeated-box plan; whatever `planRealtimePreviewGaussianBlur` reports when the plan's
///         request is checked again; the image, selection and memory failures
///         `convolveImageSeparable` documents; or `ErrorCode::OutOfRange` when the fused form's
///         per-worker scratch cannot be represented.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
blurImageRealtimePreviewGaussian(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                 const ImageLayout& sourceLayout, void* destination,
                                 const ImageLayout& destinationLayout, const GaussianPlan& plan,
                                 const SpatialSelection& selection = {});

/// Blurs a checked typed image view with a Gaussian.
///
/// The typed spelling of `blurImageGaussian`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan what to run.
/// @param selection which samples mean something.
/// @return whatever `blurImageGaussian` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<SpatialFilterResult>
blurGaussian(const ExecutionContext& execution, const ImageView<const SampleT>& source,
             const ImageView<SampleT>& destination, const GaussianPlan& plan, const SpatialSelection& selection = {})
{
    return blurImageGaussian(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                             destination.layout(), plan, selection);
}

/// Blurs a checked typed image view through the retained real-time-preview approximation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only preview view.
/// @param destination the checked writable preview view.
/// @param plan what to run, from `planRealtimePreviewGaussianBlur`.
/// @param selection which samples mean something.
/// @return whatever `blurImageRealtimePreviewGaussian` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<SpatialFilterResult>
blurRealtimePreviewGaussian(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                            const ImageView<SampleT>& destination, const GaussianPlan& plan,
                            const SpatialSelection& selection = {})
{
    return blurImageRealtimePreviewGaussian(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                            destination.data(), destination.layout(), plan, selection);
}

} // namespace an
