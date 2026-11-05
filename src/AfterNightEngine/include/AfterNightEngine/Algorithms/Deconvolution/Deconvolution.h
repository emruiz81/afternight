// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Undoing a blur whose shape is stated.
///
/// Every other neighbourhood operation in this engine takes an image and produces a smoother one.
/// This domain goes the other way: it takes an image, a point spread function saying how a point
/// source was smeared across it, and produces the image that would have been recorded had it not
/// been. That is an inverse problem rather than a filter, and the three things that follow from it
/// shape the whole file.
///
/// **A point spread function is a checked plane the caller owns, not a container this engine hands
/// back.** It is small, it is often reused across a hundred frames, and it frequently comes from
/// somewhere other than this header - a measured star profile, a file, another engine. So the shape
/// builders here write into a caller's plane whose size they will tell you in advance, and every
/// entry point takes `PlaneView<const float>`. Normalisation to unit sum happens inside the
/// operation, over its own scratch, so the caller's plane is never written by a deconvolution.
///
/// **The arithmetic is Float32 and that is a decision, not an omission.** The input samples are
/// Float32 or narrower and the transform library computes in single precision, so a double-precision
/// working plane would buy accuracy the data never had. The working planes are Float32 throughout
/// and only the flux reductions accumulate in Float64 - a frame's positive total is a sum of tens of
/// millions of terms - which is the one place the width is buying something.
///
/// **A run says why it stopped.** An iterative deconvolution that has converged and one that has run
/// out of iterations produce the same picture and mean different things, and a caller that cannot
/// tell them apart cannot choose an iteration count. Every result here carries the iterations that
/// ran, the change the last one made, and whether a stopping rule ended it.
///
/// A frame smaller than its kernel is refused, a rejected sample keeps its own destination, the white
/// level is stated rather than assumed to be one, cancellation returns a failure rather than
/// throwing, an explicit chroma mode over a frame that is not three channels is refused, and a kernel
/// with no positive energy is `ErrorCode::InvalidData`. A point spread function derived from a star
/// list is not built here; it belongs with star detection and profiling.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

class ExecutionContext;

/// The widest point spread function either method accepts, in samples.
///
/// A two-hundred-and-fifty-five by two-hundred-and-fifty-five kernel is sixty-five thousand
/// multiplies per output sample on the direct path and a transform whose padding is dominated by the
/// kernel on the other; past it the request is describing something other than an optical blur.
inline constexpr std::uint32_t kMaximumPsfExtent = 255u;

/// The narrowest point spread function either method accepts, in samples.
///
/// Three, because a one-sample kernel is the identity and an even-sized one has no centre to put the
/// source at.
inline constexpr std::uint32_t kMinimumPsfExtent = 3u;

/// The most iterations a Richardson-Lucy request may ask for.
///
/// Five hundred is far past the point where the iteration count is the parameter worth raising: the
/// method amplifies noise monotonically, and a run that needs more than this needs regularisation or
/// a better point spread function instead.
inline constexpr std::uint32_t kMaximumDeconvolutionIterations = 500u;

/// The most edge-taper passes a request may ask for.
inline constexpr std::uint32_t kMaximumEdgeTaperIterations = 16u;

/// The smallest positive number either method divides by.
///
/// Below this a stated guard is doing nothing and the ratios the method forms are unbounded.
inline constexpr double kMinimumDeconvolutionGuard = 1.0e-30;

/// The full width at half maximum of a Gaussian, in units of its standard deviation.
///
/// Two times the square root of two times the natural logarithm of two, written out so the constant
/// is checkable rather than copied.
inline constexpr double kFwhmPerSigma = 2.354820045030949382;

/// Which shape a synthetic point spread function has.
enum class PsfProfile : std::uint8_t
{
    /// A two-dimensional Gaussian, which is what atmospheric seeing produces to first order.
    Gaussian = 0u,

    /// A Moffat profile, whose wings fall off as a power law rather than exponentially and which
    /// therefore describes a real stellar image better than a Gaussian does.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Moffat = 1u
};

/// Which convolution path an operation takes.
enum class DeconvolutionConvolution : std::uint8_t
{
    /// The engine decides from the kernel's area, choosing the transform once it reaches thirty-one
    /// squared samples, where the transform's own overhead is repaid. The two paths agree on the
    /// answer, so the choice is a cost.
    Automatic = 0u,

    /// Every convolution is a direct weighted sum over the kernel's footprint.
    Direct = 1u,

    /// Every convolution is a multiplication of two spectra.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Transform = 2u
};

/// What holds an iterative deconvolution back from amplifying noise.
enum class DeconvolutionRegularization : std::uint8_t
{
    /// Nothing does. The update is the plain ratio.
    None = 0u,

    /// The update is divided by one minus a multiple of the divergence of the estimate's unit
    /// gradient, the total-variation term, so the correction of a sample standing above its
    /// neighbours is scaled down and that of one standing below them is scaled up: the oscillation
    /// the plain method grows is resisted.
    TotalVariation = 1u,

    /// The update is divided by one plus a multiple of the relative Frobenius norm of the Hessian,
    /// which penalises curvature rather than gradient and therefore does not flatten ramps.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    HessianFrobenius = 2u
};

/// How an iteration's correction reaches the estimate.
enum class DeconvolutionUpdate : std::uint8_t
{
    /// The estimate is multiplied by the correction, which is the method as published and keeps a
    /// non-negative estimate non-negative for free.
    Multiplicative = 0u,

    /// The estimate is moved a stated step along the correction, which converges more slowly and is
    /// stabler when the point spread function is a poor fit.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    GradientDescent = 1u
};

/// How a colour image is deconvolved.
enum class DeconvolutionColor : std::uint8_t
{
    /// The engine decides: a three-channel image is deconvolved through its luminance and anything
    /// else channel by channel.
    Automatic = 0u,

    /// Every channel is deconvolved independently, which is correct when the blur differs between
    /// them and which can move colour when it does not.
    PerChannel = 1u,

    /// One luminance channel is deconvolved and the three channels are rescaled by the ratio it
    /// moved, so hue and saturation are kept. Two cases still move colour: a pixel whose luminance is
    /// at or below `1e-12` takes the restored luminance as a neutral grey, and a channel clamped at
    /// the white level loses its ratio to the others. Requires exactly three channels.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    LuminancePreservingChroma = 2u
};

/// Whether the frame's edges are softened before the inverse problem is solved.
///
/// A deconvolution assumes the recorded frame is a blur of something; at the frame's edge that is
/// false, because the blur mixed in light from outside the frame. Tapering replaces the edge with a
/// blurred version of itself so the assumption holds again, at the cost of the outermost samples
/// carrying less information than they did.
enum class DeconvolutionEdgeTaper : std::uint8_t
{
    /// The engine decides: the stated radius when there is one, otherwise the kernel's larger side.
    Automatic = 0u,

    /// No taper. The edge is treated as if the blur had not reached across it.
    Off = 1u,

    /// The stated radius, whatever it is, including zero.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Manual = 2u
};

/// The three coefficients of the Rec. 709 luminance, used when a colour mode preserves chroma.
///
/// Named rather than written inline because the same three numbers decide what "brightness" means
/// for the colour mode and for nothing else, and a caller reading a result needs to know which
/// luminance was deconvolved.
inline constexpr double kLuminanceRed = 0.2126;

/// The green coefficient of the Rec. 709 luminance.
inline constexpr double kLuminanceGreen = 0.7152;

/// The blue coefficient of the Rec. 709 luminance.
inline constexpr double kLuminanceBlue = 0.0722;

/// What a caller asks a synthetic point spread function for.
struct PsfRequest
{
    /// Which shape.
    PsfProfile profile = PsfProfile::Gaussian;

    /// The full width at half maximum across, in pixels. Must be finite and above zero.
    double fwhmX = 3.0;

    /// The full width at half maximum down, in pixels. Must be finite and above zero.
    double fwhmY = 3.0;

    /// How far the profile's own axes are rotated from the image's, in degrees. Must be finite.
    double angleDegrees = 0.0;

    /// The Moffat exponent, read only for that profile. Must be finite and above one, below which
    /// the profile has no finite integral.
    double beta = 4.5;

    /// The side of the square kernel, in samples, or zero to derive one from the widths.
    ///
    /// A stated size must be odd and between `kMinimumPsfExtent` and `kMaximumPsfExtent`.
    std::uint32_t size = 0u;
};

/// The side a request's point spread function will have.
///
/// Deterministic and allocation-free, so a caller can size its plane before building anything. A
/// Gaussian is four standard deviations of its wider axis either side of the centre, rounded up and
/// never below five samples; a Moffat is four times its wider full width, rounded up to the next odd
/// side, because its wings carry weight much further out.
///
/// @param request the profile, the widths and the stated size.
/// @return the odd side, in samples; `ErrorCode::InvalidArgument` for a non-finite or non-positive
///         width, a non-finite angle, a Moffat exponent at or below one, a profile outside its
///         enumeration, or a stated size that is even; or `ErrorCode::OutOfRange` for a stated or
///         derived size outside `[kMinimumPsfExtent, kMaximumPsfExtent]`, carrying the size and the
///         limit it crossed - or, for widths whose derived size is refused before it is computed,
///         the largest representable count in place of the size.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t> psfExtent(const PsfRequest& request);

/// Writes a normalised synthetic point spread function into a caller's plane.
///
/// The samples sum to one, so a convolution by the result preserves total flux to within the
/// rounding of each sample's narrowing to Float32 and its multiplication by the Float32 reciprocal of
/// the sum.
///
/// Thread-safe with respect to `request`, which is only read. Allocation-free, single-threaded, and
/// takes no execution: building a kernel of at most two hundred and fifty-five samples a side is
/// bounded work a caller can do anywhere.
///
/// @param request the profile, the widths and the stated size.
/// @param destination a square plane whose side is exactly `psfExtent(request)`.
/// @return success; whatever `psfExtent` refuses; or `ErrorCode::InvalidArgument` when the
///         destination is not the square plane of that side.
[[nodiscard]] AFTERNIGHTENGINE_API Status buildPsf(const PsfRequest& request, PlaneView<float> destination);

/// Whether a plane is a point spread function this domain will accept.
///
/// Checked here rather than inside each operation so a caller can validate a kernel it obtained
/// somewhere else - a measured star profile, a file - once instead of per frame.
///
/// Thread-safe and allocation-free.
///
/// @param psf the kernel.
/// @return success; `ErrorCode::InvalidArgument` for a kernel with an even side or a sample that is
///         not finite or is negative; `ErrorCode::OutOfRange` for a side outside
///         `[kMinimumPsfExtent, kMaximumPsfExtent]`; or `ErrorCode::InvalidData` when the samples do
///         not sum to something positive, which is a kernel that cannot be normalised.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkPsf(PlaneView<const float> psf);

/// What every deconvolution in this file shares.
struct DeconvolutionCommon
{
    /// What a fully exposed sample is, in the image's own sample units.
    ///
    /// The ceiling `clampToWhite` holds the result at or below; nothing else reads it, and the
    /// luminance colour mode applies its coefficients to the stored samples as they are. Must be
    /// finite and above zero.
    double whiteLevel = 1.0;

    /// The smallest number the method divides by, in the image's own sample units.
    ///
    /// Must be finite and at least `kMinimumDeconvolutionGuard`.
    double guard = 1.0e-7;

    /// Whether the estimate is rescaled so its positive sum matches that of the plane the method
    /// solves, which is the observation after substitution and taper.
    ///
    /// A deconvolution should preserve flux and does not exactly, because the estimate is clipped at
    /// zero. A Richardson-Lucy run rescales after every iteration and a Wiener filter once; the clamp
    /// to `whiteLevel` comes afterwards and can lower the total again.
    bool preserveFlux = true;

    /// Whether the estimate is held at or above zero.
    bool nonNegative = true;

    /// Whether the result is held at or below `whiteLevel`.
    bool clampToWhite = true;

    /// How a coordinate outside the frame is answered, on both convolution paths.
    ///
    /// Must be one of the modes that resolve a coordinate; a constant border is refused, because a
    /// constant outside a frame is exactly the discontinuity the taper exists to remove.
    KernelBorderMode border = KernelBorderMode::ReflectExcludingEdge;

    /// Which convolution path to take.
    DeconvolutionConvolution convolution = DeconvolutionConvolution::Automatic;

    /// How a colour image is treated.
    DeconvolutionColor color = DeconvolutionColor::Automatic;

    /// Whether the frame's edges are softened first.
    DeconvolutionEdgeTaper edgeTaper = DeconvolutionEdgeTaper::Automatic;

    /// How far the taper reaches in from each edge, in pixels. Must be finite and not negative.
    double edgeTaperRadius = 0.0;

    /// How many times the taper blurs and blends, with zero running no taper. Must be at most
    /// `kMaximumEdgeTaperIterations`.
    std::uint32_t edgeTaperIterations = 3u;

    /// What stands in for a sample the validity rule rejects, in the image's own sample units.
    ///
    /// A deconvolution reads a neighbourhood but is not a neighbourhood filter: its estimate is
    /// global, so a rejected sample cannot be answered per neighbour and is replaced once before the
    /// first iteration. Its own destination sample is still left untouched on the way out. Must be
    /// finite.
    double invalidSample = 0.0;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// What a caller asks a Richardson-Lucy deconvolution for.
struct RichardsonLucyRequest
{
    /// What every deconvolution shares.
    DeconvolutionCommon common{};

    /// How many iterations to run, at most. Must be between one and
    /// `kMaximumDeconvolutionIterations`.
    std::uint32_t iterations = 20u;

    /// Which regularisation holds the update back.
    DeconvolutionRegularization regularization = DeconvolutionRegularization::TotalVariation;

    /// How strongly, with zero meaning the regularisation does not apply however it is named. Must
    /// be finite and not negative.
    ///
    /// Whatever the strength, the divisor the regularisation produces is held between a quarter and
    /// four, so it can scale one iteration's correction by at most four either way.
    double regularizationStrength = 0.0;

    /// How the correction reaches the estimate.
    DeconvolutionUpdate update = DeconvolutionUpdate::Multiplicative;

    /// How far the estimate moves along the correction, under gradient descent. Must be finite and
    /// above zero when that update is chosen.
    double gradientDescentStep = 0.0003;

    /// The distance from one that a compressed correction approaches and never reaches, or zero for
    /// no compression. Must be finite and not negative; a positive value below `1e-6` acts as `1e-6`.
    ///
    /// Every correction is compressed, not only a large one: its distance from one, `d`, becomes
    /// `d / (1 + |d| / damping)`, which leaves a small correction nearly as it was. This is what stops
    /// one bright sample driving its neighbourhood: a correction of a hundred and one of one and a
    /// hundredth are both moved towards one by the same rule, which bounds the distance a single
    /// iteration can travel without bounding the total. The compression is applied before the
    /// regularisation divisor.
    double damping = 0.0;

    /// The mean absolute change, in the image's own sample units, at or below which the run stops,
    /// or zero for no such rule. Must be finite and not negative.
    ///
    /// The change is averaged over every sample of the plane and measured before that iteration's
    /// flux rescaling.
    double stoppingDelta = 0.0;

    /// The same, as a fraction of the mean positive signal of the estimate the iteration started
    /// from: its positive sum over its sample count, held at or above `guard`. Must be finite and not
    /// negative.
    double relativeStoppingDelta = 0.0;
};

/// What a caller asks a Wiener deconvolution for.
struct WienerRequest
{
    /// What every deconvolution shares.
    ///
    /// The convolution field is read only for the taper: the filter itself is a spectrum
    /// multiplication and has no direct form.
    DeconvolutionCommon common{};

    /// The assumed ratio of noise power to signal power, which is what keeps the inverse filter
    /// finite where the point spread function has no response. Must be finite and above zero.
    double regularization = 0.01;
};

/// A validated Richardson-Lucy request, with the convolution path decided.
///
/// Trivially copyable and allocation-free.
class RichardsonLucyPlan
{
public:
    /// A plan describing no deconvolution, which no execution accepts.
    RichardsonLucyPlan() noexcept = default;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const RichardsonLucyRequest& request() const noexcept;

    /// Whether the convolutions will go through the transform.
    ///
    /// @return true for the transform path.
    [[nodiscard]] AFTERNIGHTENGINE_API bool usesTransform() const noexcept;

    /// The width of the point spread function this plan checked.
    ///
    /// @return the width in samples, or zero for a plan describing no deconvolution.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t psfWidth() const noexcept;

    /// The height of the point spread function this plan checked.
    ///
    /// @return the height in samples, or zero for a plan describing no deconvolution.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t psfHeight() const noexcept;

    /// How far the taper actually reaches, in pixels.
    ///
    /// @return the resolved radius, which is zero when no taper runs.
    [[nodiscard]] AFTERNIGHTENGINE_API double taperRadius() const noexcept;

    /// How many times the operation walks the whole image.
    ///
    /// @return two convolutions per iteration plus one taper pass per taper iteration when a taper
    ///         runs, or zero for a plan describing no deconvolution.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// Builds the only validated shape this type may hold.
    friend AFTERNIGHTENGINE_API Result<RichardsonLucyPlan> planRichardsonLucy(const RichardsonLucyRequest& request,
                                                                              PlaneView<const float> psf);

    /// Records pieces validated by the friend builder.
    ///
    /// @param request the iterations, the stopping rules and the common fields.
    /// @param psfWidth the kernel's width, in samples.
    /// @param psfHeight the kernel's height, in samples.
    RichardsonLucyPlan(const RichardsonLucyRequest& request, std::uint32_t psfWidth, std::uint32_t psfHeight) noexcept;

    /// The request this plan records.
    RichardsonLucyRequest m_request{};

    /// The resolved taper radius.
    double m_taperRadius = 0.0;

    /// Whether the transform path was chosen.
    bool m_usesTransform = false;

    /// Width of the checked point spread function.
    std::uint32_t m_psfWidth = 0u;

    /// Height of the checked point spread function.
    std::uint32_t m_psfHeight = 0u;

    /// Whether this plan was built by a validated request.
    bool m_valid = false;
};

/// A validated Wiener request.
///
/// Trivially copyable and allocation-free.
class WienerPlan
{
public:
    /// A plan describing no deconvolution, which no execution accepts.
    WienerPlan() noexcept = default;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const WienerRequest& request() const noexcept;

    /// The width of the point spread function this plan checked.
    ///
    /// @return the width in samples, or zero for a plan describing no deconvolution.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t psfWidth() const noexcept;

    /// The height of the point spread function this plan checked.
    ///
    /// @return the height in samples, or zero for a plan describing no deconvolution.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t psfHeight() const noexcept;

    /// Whether the edge taper will convolve through the transform.
    ///
    /// The Wiener solve itself always uses the transform; this reports the separately requested
    /// convolution path for the taper.
    ///
    /// @return true for a transform taper, false for a direct taper or when no taper runs.
    [[nodiscard]] AFTERNIGHTENGINE_API bool taperUsesTransform() const noexcept;

    /// How far the taper actually reaches, in pixels.
    ///
    /// @return the resolved radius, which is zero when no taper runs.
    [[nodiscard]] AFTERNIGHTENGINE_API double taperRadius() const noexcept;

    /// How many times the operation walks the whole image.
    ///
    /// @return one filter pass plus one taper pass per taper iteration when a taper runs, or zero for
    ///         a plan describing no deconvolution.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// Builds the only validated shape this type may hold.
    friend AFTERNIGHTENGINE_API Result<WienerPlan> planWiener(const WienerRequest& request, PlaneView<const float> psf);

    /// Records pieces validated by the friend builder.
    ///
    /// @param request the regularisation and the common fields.
    /// @param psfWidth the kernel's width, in samples.
    /// @param psfHeight the kernel's height, in samples.
    WienerPlan(const WienerRequest& request, std::uint32_t psfWidth, std::uint32_t psfHeight) noexcept;

    /// The request this plan records.
    WienerRequest m_request{};

    /// The resolved taper radius.
    double m_taperRadius = 0.0;

    /// Whether the taper's convolution path was resolved to the transform.
    bool m_taperUsesTransform = false;

    /// Width of the checked point spread function.
    std::uint32_t m_psfWidth = 0u;

    /// Height of the checked point spread function.
    std::uint32_t m_psfHeight = 0u;

    /// Whether this plan was built by a validated request.
    bool m_valid = false;
};

/// What one deconvolution did.
struct DeconvolutionResult
{
    /// The neighbourhood family's common report.
    ///
    /// The selection and the outputs are summed over the channels, with every sample the validity
    /// rule did not reject counted as written; the image passes are the plan's; the working bytes are
    /// the workspace and the convolution engine together; the precision is always
    /// `ComputePrecision::Float32`. The radius fields stay zero and `readInPlace` stays false, because
    /// the source is always gathered into working planes.
    SpatialFilterResult filter{};

    /// How many stored samples the operation actually moved.
    std::uint64_t samplesChanged = 0u;

    /// How many samples the validity rule rejected and the substitute stood in for.
    std::uint64_t substitutedSamples = 0u;

    /// How many iterations ran, at most, over the channels the operation deconvolved.
    ///
    /// Iterative methods only; a Wiener filter reports zero.
    std::uint32_t iterationsRun = 0u;

    /// Whether a stopping rule ended a channel before its iteration count did.
    ///
    /// The number that separates a converged run from an exhausted one.
    bool stoppedEarly = false;

    /// The mean absolute change the last iteration of the last channel solved made, in the image's
    /// own sample units. Zero for a Wiener filter.
    double lastChange = 0.0;

    /// The same, as a fraction of the estimate's mean positive signal, as
    /// `RichardsonLucyRequest::relativeStoppingDelta` measures it.
    double lastRelativeChange = 0.0;

    /// What the flux correction multiplied the last channel by, or one when none was applied.
    ///
    /// A number far from one says the estimate lost or gained energy the method should have kept,
    /// which is the honest early warning that the point spread function does not fit the frame.
    double fluxScale = 1.0;

    /// Whether the convolutions went through the transform.
    bool usedTransform = false;

    /// Whether the edge taper convolved through the transform.
    ///
    /// False when no taper ran. A Wiener solve can report `usedTransform` true and this false,
    /// because its inverse filter is necessarily spectral while its taper honours the requested
    /// direct convolution path.
    bool taperUsedTransform = false;

    /// The transform's width, in samples, or zero when none ran.
    std::uint32_t transformWidth = 0u;

    /// The transform's height, in samples, or zero when none ran.
    std::uint32_t transformHeight = 0u;

    /// Whether one luminance channel was deconvolved and the colours rescaled to match it.
    bool preservedChroma = false;
};

/// Validates a Richardson-Lucy request against a kernel.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the iterations, the stopping rules and the common fields.
/// @param psf the kernel the operation will run against, which is checked as `checkPsf` checks it.
/// @return the plan; whatever `checkPsf` refuses; `ErrorCode::InvalidArgument` for a non-finite or
///         negative number where the field's documentation requires otherwise, a field outside its
///         enumeration, a constant or inapplicable border, an unknown output contract, a malformed
///         write-back policy, or one naming `DomainMapping::FullScale`; or `ErrorCode::OutOfRange`
///         for zero iterations, more than `kMaximumDeconvolutionIterations`, or more than
///         `kMaximumEdgeTaperIterations` taper passes, each carrying the value and its limit, or for
///         a numeric field that cannot be represented by the Float32 solver.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RichardsonLucyPlan> planRichardsonLucy(const RichardsonLucyRequest& request,
                                                                                 PlaneView<const float> psf);

/// Validates a Wiener request against a kernel.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the regularisation and the common fields.
/// @param psf the kernel the operation will run against.
/// @return the plan; whatever `checkPsf` refuses; or the argument and range failures
///         `planRichardsonLucy` documents that apply to the shared fields, including an
///         unrepresentable regularisation value.
[[nodiscard]] AFTERNIGHTENGINE_API Result<WienerPlan> planWiener(const WienerRequest& request,
                                                                 PlaneView<const float> psf);

/// Recovers the image a stated blur was applied to, by repeated ratio correction.
///
/// The untyped entry point, and the one the exported library carries. Each iteration blurs the
/// current estimate by the point spread function, divides the observed image by that blur, blurs the
/// ratio by the mirrored point spread function, and moves the estimate along the result. The method
/// converges on the maximum-likelihood image under Poisson statistics, which is what a photon count
/// has, and it amplifies noise while it does so - which is what the regularisation, the damping and
/// the stopping rules are for.
///
/// Thread-safe with respect to `execution`, the source, the kernel and the selection, all of which
/// are only read. Runs on the threads the operation was admitted, checks cancellation at iteration
/// and row-block boundaries, and is not failure-atomic.
///
/// Progress is reported through the execution's sink once per iteration, in a total of the
/// request's iterations for every channel it solves, and ends at that total when the stopping rule
/// ends the iterations early.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the observed image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the recovered samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param psf the point spread function, which is read and never written.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return what the operation did; whatever `checkPsf` refuses; `ErrorCode::NotInitialized` for an
///         empty execution; `ErrorCode::InvalidArgument` for a plan that has not been validated, a
///         kernel whose geometry is not the plan's, a null pointer, a layout whose element size is
///         not the representation's, an incoherent selection mask, or a luminance colour mode over
///         an image that does not have three channels; `ErrorCode::IncompatibleArguments` for
///         disagreeing extents, buffers that share a byte, a mask that does not cover the image, or
///         an output promise Float32 arithmetic cannot keep; `ErrorCode::OutOfRange` for a frame
///         smaller than the kernel; `ErrorCode::LimitExceeded` for a padded transform too large to
///         plan; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was
///         stopped; `ErrorCode::ExternalFailure` when the transform library declines a plan; or the
///         memory resource's refusal - `ErrorCode::MemoryBudgetExceeded`, carrying the requested,
///         available and limit byte counts, or `ErrorCode::AllocationFailed`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DeconvolutionResult>
deconvolveImageRichardsonLucy(const ExecutionContext& execution, SampleType sampleType, const void* source,
                              const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                              PlaneView<const float> psf, const RichardsonLucyPlan& plan,
                              const SpatialSelection& selection = {});

/// Recovers the image a stated blur was applied to, by repeated ratio correction, over checked typed
/// views.
///
/// The typed spelling of `deconvolveImageRichardsonLucy`, which owns every check and every
/// computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param psf the point spread function.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return whatever `deconvolveImageRichardsonLucy` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<DeconvolutionResult>
deconvolveRichardsonLucy(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                         const ImageView<SampleT>& destination, PlaneView<const float> psf,
                         const RichardsonLucyPlan& plan, const SpatialSelection& selection = {})
{
    return deconvolveImageRichardsonLucy(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                         destination.data(), destination.layout(), psf, plan, selection);
}

/// Recovers the image a stated blur was applied to, in one pass through the frequency domain.
///
/// The untyped entry point, and the one the exported library carries. The observed spectrum is
/// divided by the point spread function's, with the divisor held away from zero by a stated
/// noise-to-signal ratio, so a frequency the blur destroyed is not amplified by the noise that
/// remains there: each frequency of the answer is the observed one times the conjugate of the
/// kernel's response, over that response's squared magnitude plus the stated ratio. One pass rather
/// than many, no non-negativity constraint inside the solve - `nonNegative` clamps the answer
/// afterwards - and the minimum mean-square-error linear answer for the model it assumes, which is
/// why it is the right first thing to try and the wrong thing to reach for when the blur is strong.
///
/// Thread-safe with respect to `execution`, the source, the kernel and the selection, all of which
/// are only read. Runs on the threads the operation was admitted, checks cancellation at stage
/// boundaries, and is not failure-atomic. Reports no progress: it is one pass over its input.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the observed image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the recovered samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param psf the point spread function, which is read and never written.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return what the operation did, or the same failures `deconvolveImageRichardsonLucy` documents.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DeconvolutionResult>
deconvolveImageWiener(const ExecutionContext& execution, SampleType sampleType, const void* source,
                      const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                      PlaneView<const float> psf, const WienerPlan& plan, const SpatialSelection& selection = {});

/// Recovers the image a stated blur was applied to, in one frequency-domain pass, over checked typed
/// views.
///
/// The typed spelling of `deconvolveImageWiener`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param psf the point spread function.
/// @param plan the validated request.
/// @param selection which samples mean something.
/// @return whatever `deconvolveImageWiener` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<DeconvolutionResult>
deconvolveWiener(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                 const ImageView<SampleT>& destination, PlaneView<const float> psf, const WienerPlan& plan,
                 const SpatialSelection& selection = {})
{
    return deconvolveImageWiener(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                                 destination.layout(), psf, plan, selection);
}

/// What every operation in this file does to the image it writes.
///
/// A deconvolution changes samples and nothing else. The extent, the representation and the channel
/// schema are the source's; a sharpened frame is still a frame of the same thing; nothing moves a
/// star, because the point spread function is centred; and no source card and no output decision is
/// invalidated by it.
///
/// Validity is preserved, and that is a claim about the arithmetic: a rejected sample is replaced
/// once before the first iteration rather than spread by it, and its own destination sample is left
/// untouched, so the operation can neither create nor destroy meaninglessness.
inline constexpr OperationEffect kDeconvolutionEffect{};

} // namespace an
