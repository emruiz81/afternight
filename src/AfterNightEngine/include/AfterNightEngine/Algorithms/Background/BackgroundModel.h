// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Describing the light that is not the picture, and taking it away.
///
/// A frame of the sky carries two things: the objects that were photographed, and a smooth
/// unwanted illumination - light pollution, a moon, a vignette the flat did not remove - that
/// varies across the frame and belongs to nothing in it. Every operation in this file exists to
/// separate the second from the first, and the separation is written as four stages that name
/// themselves rather than as one call that does all four invisibly.
///
/// **Sample, fit, evaluate, apply.** Sampling asks the frame where the background can be measured
/// and returns the measurements; fitting turns measurements into the coefficients of a stated
/// surface; evaluating turns coefficients into a picture of that surface; applying takes the
/// surface away from the frame. Each stage is a call, and the value between two stages is a small
/// typed record the caller owns, so a caller can move one sample, or reuse one fit across a hundred
/// frames of the same session, without redoing the rest.
///
/// **The caller owns every array this file fills.** Sites, coefficients and model planes are
/// spans and views the caller sized, and each stage says in advance how large its output will be -
/// `planBackgroundSampleGrid` for the sites, `backgroundModelTermCount` for the coefficients. A
/// background model is small, it is frequently kept beside a frame for the length of a session,
/// and nothing here should decide how long it lives.
///
/// **The working numbers are the stored numbers.** This family reads a sample and carries it
/// across as the number it is, exactly as `Algorithms/Spatial/SpatialFilter.h` states for every
/// neighbourhood filter. A threshold, a guard, a level and a clamp are all written in the image's
/// own units, and a clamp happens only when one is stated, so a UInt16 frame and a Float32 frame of
/// the same scene get the same correction and a frame whose samples legitimately exceed one keeps
/// them.
///
/// **A degenerate fit is reported, never hidden.** A polynomial whose sites do not determine every
/// term - too few of them for the degree, or all of them on one line - drops the terms they do not
/// determine, the later ones in a basis ordered lowest degree first, and says how many in
/// `BackgroundFitResult::droppedTerms`. A thin-plate system that is singular, as it is when the
/// sites all lie on one line, is refused with `ErrorCode::InvalidData`. Either way a caller can tell
/// a model that quietly became a plane from the one it asked for.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

class ExecutionContext;

/// The most channels a background sample carries a measurement for.
///
/// A site is one position and one median per channel, and the medians live inside the record so
/// that a caller holding a span of sites holds everything the fit needs. Four, because that covers
/// a monochrome frame, a colour frame and a colour frame with an alpha channel, and because a
/// bounded array is what keeps the record standard-layout. An image with more channels is refused
/// by name rather than sampled partially.
inline constexpr std::uint32_t kMaxBackgroundChannels = 4u;

/// The smallest square a site may be measured over, in samples.
///
/// Five, because a median over fewer than a couple of dozen samples is a value rather than a
/// statistic, and because an odd side is what gives the box a centre to sit on.
inline constexpr std::uint32_t kMinBackgroundSampleExtent = 5u;

/// The largest square a site may be measured over, in samples.
///
/// Past this the box is no longer sampling a background: it is averaging over structure the model
/// is meant to leave alone.
inline constexpr std::uint32_t kMaxBackgroundSampleExtent = 255u;

/// The most sites one grid may hold.
///
/// A sampling and a polynomial fit cost work in proportion to the site count, and a thin-plate
/// spline, which `kMaxRadialBasisSites` bounds more tightly, costs its cube; the ceiling is what
/// keeps a mistyped grid density from turning into a call that never returns.
inline constexpr std::uint32_t kMaxBackgroundSites = 4096u;

/// The most sites a radial-basis fit accepts.
///
/// The system it solves has one row per site plus three, and it is solved by elimination, so the work
/// grows as the cube of this number and the matrix as its square. A thousand sites is a matrix of
/// eight megabytes and a solve of a third of a billion operations, which is the point past which a
/// caller wanting a denser model wants a polynomial instead.
inline constexpr std::uint32_t kMaxRadialBasisSites = 1024u;

/// The highest total degree a polynomial background may have.
///
/// Six, which is twenty-eight terms; past it the surface has more freedom than a smooth illumination
/// has and starts describing the objects instead.
inline constexpr std::uint32_t kMaxBackgroundPolynomialDegree = 6u;

/// The coarsest lattice a model may be evaluated on before it is interpolated, in pixels.
///
/// A stride of one evaluates the model at every pixel and interpolates nothing.
inline constexpr std::uint32_t kMaxBackgroundEvaluationStride = 64u;

/// How many points a side the lattice the applied level is measured over holds, at most.
///
/// A background model is smooth by construction, so its median over a lattice this fine and its
/// median over every pixel agree far beyond the precision anything downstream keeps. Bounding the
/// lattice is what makes the measurement cost the same for a twenty-megapixel frame as for a small
/// one, and it is why no entry point here allocates a full-resolution copy of the model to find a
/// number out of it.
inline constexpr std::uint32_t kBackgroundLevelLatticeExtent = 512u;

/// The three coefficients of the Rec. 709 luminance, used when a site is placed by that luminance.
///
/// Named rather than written inline because they decide which pixels a background sampling calls
/// dark, and a caller reading a rejected site needs to know what "dark" meant.
inline constexpr double kBackgroundLuminanceRed = 0.2126;

/// The green coefficient of the Rec. 709 luminance.
inline constexpr double kBackgroundLuminanceGreen = 0.7152;

/// The blue coefficient of the Rec. 709 luminance.
inline constexpr double kBackgroundLuminanceBlue = 0.0722;

/// Which single number a colour site is accepted or rejected on.
///
/// A site is kept or dropped once for the whole pixel rather than once per channel, because a star
/// is a star in every channel and a model fitted to sites chosen differently per channel would put
/// a colour gradient where the frame has none. The number is formed from the site's per-channel
/// medians rather than from a luminance per pixel.
enum class BackgroundLuminance : std::uint8_t
{
    /// The unweighted mean of every channel, which treats a narrowband channel set as evenly as a
    /// colour one.
    ChannelMean = 0u,

    /// The Rec. 709 luminance of the first three channels, for a frame whose channels really are
    /// red, green and blue. An image with fewer than three channels falls back to the mean of the
    /// channels it has.
    Rec709 = 1u,

    /// The first channel alone, which is the cheapest and is what a caller wants when one channel
    /// is the reference the others are aligned to.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    FirstChannel = 2u
};

/// Whether a value names a luminance rule this build implements.
///
/// @param luminance the value to inspect.
/// @return true for the three rules.
[[nodiscard]] constexpr bool isBackgroundLuminance(BackgroundLuminance luminance) noexcept
{
    return luminance <= BackgroundLuminance::FirstChannel;
}

/// The name of one luminance rule, for a diagnostic a person reads.
///
/// @param luminance which rule.
/// @return a static string, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* backgroundLuminanceName(BackgroundLuminance luminance) noexcept;

/// One measured background site.
///
/// Trivially copyable and standard-layout, so a caller may hold an array of these anywhere.
struct BackgroundSample
{
    /// The column of the site's centre, in pixels, under the pixel-centre convention: the first
    /// pixel's centre is zero.
    double x = 0.0;

    /// The row of the site's centre, in pixels, under the same convention.
    double y = 0.0;

    /// The median of the samples of the site's square the validity rule admits, in the image's own
    /// units, one entry per channel and the rest left at zero.
    std::array<double, kMaxBackgroundChannels> median{};

    /// The single number the site was accepted on, under the request's luminance rule.
    double luminance = 0.0;

    /// The side of the square the medians were taken over, in samples.
    std::uint32_t extent = 0u;

    /// How many channels of `median` hold a measurement.
    std::uint32_t channelCount = 0u;

    /// The fewest samples any one channel's median was taken over: `extent` squared unless the
    /// validity rule rejected some samples of the square.
    std::uint64_t sampleCount = 0u;

    /// Two sites agree when every field agrees exactly.
    friend constexpr bool operator==(const BackgroundSample& left, const BackgroundSample& right) noexcept = default;
};

/// What a caller asks a background sampling for.
struct BackgroundSampleRequest
{
    /// The side of the square each site is measured over, in samples.
    ///
    /// Must be odd and within `[kMinBackgroundSampleExtent, kMaxBackgroundSampleExtent]`.
    std::uint32_t extent = 25u;

    /// How many sites one row of the grid holds. Must be at least two.
    std::uint32_t samplesPerLine = 20u;

    /// How far above the frame's own background level a site may sit and still be kept, in
    /// multiples of the median absolute deviation of the site luminances.
    ///
    /// Must be finite and not negative. Zero keeps only the sites at or below the level itself.
    double tolerance = 1.0;

    /// Which single number a site is accepted on.
    BackgroundLuminance luminance = BackgroundLuminance::ChannelMean;

    /// Whether a site whose luminance is not above zero is rejected.
    ///
    /// On by default, because a background at or below zero says the frame has already been
    /// over-corrected and the model would be fitted to that mistake. It is a field because a
    /// calibrated frame legitimately carries negative background samples, and refusing to model one
    /// is a decision the caller should be able to make.
    bool rejectNonPositive = true;
};

/// Where the sites of one request will be, before any sample is read.
///
/// Deterministic and allocation-free, so a caller sizes its array once and reuses it for every
/// frame of a session. The site in column `c` and row `r` of the grid is centred on pixel
/// `(originX + round(c * columnSpacing), originY + round(r * rowSpacing))`, rounded to the nearest
/// whole pixel, and sites are numbered row by row.
struct BackgroundSampleGrid
{
    /// How many sites one row holds.
    std::uint32_t columns = 0u;

    /// How many rows of sites the grid holds.
    std::uint32_t rows = 0u;

    /// The side of each site's square, in samples.
    std::uint32_t extent = 0u;

    /// The horizontal distance between two neighbouring site centres, in pixels.
    double columnSpacing = 0.0;

    /// The vertical distance between two neighbouring site centres, in pixels.
    double rowSpacing = 0.0;

    /// The column the first site's centre sits on.
    std::uint32_t originX = 0u;

    /// The row the first site's centre sits on.
    std::uint32_t originY = 0u;

    /// How many sites the grid holds, which is the array a sampling needs.
    ///
    /// @return `columns * rows`.
    [[nodiscard]] constexpr std::uint32_t siteCount() const noexcept
    {
        return columns * rows;
    }

    /// Two grids agree when every field agrees exactly.
    friend constexpr bool operator==(const BackgroundSampleGrid& left,
                                     const BackgroundSampleGrid& right) noexcept = default;
};

/// Where the sites of one request would be over one picture.
///
/// Thread-safe, allocation-free, reads no image and takes no execution: a grid is a question about
/// two extents and a box size.
///
/// The grid is centred, so a model fitted to it is not biased towards one corner. A row's sites are
/// spread across the whole width less a one-pixel margin on each side; the rows use the same
/// spacing, as many as fit the height less the same margins, and whatever height is left over is
/// split between the top and bottom margins.
///
/// @param request the box size, the density and the acceptance rule.
/// @param width how many pixels one row of the picture holds.
/// @param height how many rows the picture holds.
/// @return the grid; `ErrorCode::InvalidArgument` for a luminance rule outside its enumeration, an
///         even box side, a tolerance that is not finite or is negative, or fewer than two sites per
///         line; `ErrorCode::OutOfRange` for a box side outside
///         `[kMinBackgroundSampleExtent, kMaxBackgroundSampleExtent]`, a row of boxes wider than the
///         picture less its margins, or a grid holding more than `kMaxBackgroundSites` sites, each
///         carrying the requested value and the limit; or `ErrorCode::EmptySelection` when the
///         picture cannot hold one complete box with a one-pixel margin on every side.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BackgroundSampleGrid>
planBackgroundSampleGrid(const BackgroundSampleRequest& request, std::uint32_t width, std::uint32_t height);

/// What one background sampling did.
struct BackgroundSampleResult
{
    /// The geometry the sites were taken on.
    BackgroundSampleGrid grid{};

    /// How many sites were kept and written to the caller's array.
    std::uint32_t acceptedCount = 0u;

    /// How many sites of the grid were not kept: rejected by the threshold or the positivity rule,
    /// or not measurable because the validity rule admits no sample of some channel of their box.
    std::uint32_t rejectedCount = 0u;

    /// How many channels each accepted site carries a median for.
    std::uint32_t channelCount = 0u;

    /// The median of every measured site's luminance, which is the frame's own background level.
    double frameLevel = 0.0;

    /// The median absolute deviation of those luminances from that level, unscaled.
    double frameDeviation = 0.0;

    /// The luminance at or below which a site was kept, which is the level plus the tolerance times
    /// the deviation.
    double threshold = 0.0;

    /// How many source samples the validity rule rejected inside the measured boxes, summed over the
    /// channels.
    ///
    /// A rejected sample takes no part in its site's median rather than standing in for one, so a
    /// site over a repaired region is measured over fewer samples and says so in `sampleCount`.
    std::uint64_t rejectedSamples = 0u;

    /// Context memory held during the call and released before it returned, in bytes.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;
};

/// Measures the background over a grid of sites and keeps the ones a threshold accepts.
///
/// The untyped entry point, and the one the exported library carries. Every site of
/// `planBackgroundSampleGrid`'s grid is measured; the median of every site's luminance is the
/// frame's background level, the median absolute deviation of those luminances is its scale, and a
/// site above the level by more than the stated multiple of that scale is dropped as an object
/// rather than a background. What survives is written to the front of `sites` in row-major grid
/// order.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted and checks cancellation at site-block
/// boundaries.
///
/// The caller's array is the operation's working room as well as its output: every site of the grid
/// is measured into it in grid order and the accepted ones are then compacted to its front, which is
/// why it has to be as long as the grid rather than as long as the answer. The entries past
/// `acceptedCount` hold leftover measurements that are not part of the answer, and a call that
/// fails leaves the array holding whatever had been measured. A site is measurable only when the
/// validity rule admits at least one sample of every channel of its box.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are.
/// @param source where they start. Must not be null.
/// @param sourceLayout where they sit.
/// @param request the box size, the density and the acceptance rule.
/// @param sites where the accepted sites go, of at least the grid's site count.
/// @param selection which samples mean something.
/// @return what the sampling did; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one;
///         `ErrorCode::InvalidArgument` for a representation this build does not know, a null
///         source, a layout whose element size is not the representation's, or an explicit-mask
///         rule without its mask or a mask given to a rule that reads none;
///         `ErrorCode::IncompatibleArguments` for a mask that does not cover the image, an image of
///         more than `kMaxBackgroundChannels` channels, or an array shorter than the grid; whatever
///         `planBackgroundSampleGrid` refuses; `ErrorCode::EmptySelection` when no site could be
///         measured at all; or the engine context's refusal of the working memory.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BackgroundSampleResult>
sampleImageBackground(const ExecutionContext& execution, SampleType sampleType, const void* source,
                      const ImageLayout& sourceLayout, const BackgroundSampleRequest& request,
                      std::span<BackgroundSample> sites, const SpatialSelection& selection = {});

/// Measures the background over a grid of sites of a checked typed image view.
///
/// The typed spelling of `sampleImageBackground`, which owns every check and every measurement.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param request the box size, the density and the acceptance rule.
/// @param sites where the accepted sites go.
/// @param selection which samples mean something.
/// @return whatever `sampleImageBackground` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<BackgroundSampleResult>
sampleBackground(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                 const BackgroundSampleRequest& request, std::span<BackgroundSample> sites,
                 const SpatialSelection& selection = {})
{
    return sampleImageBackground(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), request, sites,
                                 selection);
}

/// Measures one background site the caller placed by hand.
///
/// The other half of an interactive sample list: a person adds a site where the automatic grid put
/// none, and this measures it under exactly the rules the grid used. No threshold is applied, and
/// the site is returned rather than accepted, because the caller placing it has already decided.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the calling thread and checks cancellation once, before it reads a sample.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are.
/// @param source where they start. Must not be null.
/// @param sourceLayout where they sit.
/// @param request the box size and the luminance rule; the density is not read, and the tolerance
///        is checked but not applied.
/// @param x the column the site's centre sits on.
/// @param y the row the site's centre sits on.
/// @param selection which samples mean something.
/// @return the measured site; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one;
///         `ErrorCode::InvalidArgument` for a representation this build does not know, a null
///         source, a layout whose element size is not the representation's, an explicit-mask rule
///         without its mask or a mask given to a rule that reads none, a luminance rule outside its
///         enumeration, an even box side, or a tolerance that is not finite or is negative;
///         `ErrorCode::IncompatibleArguments` for a mask that does not cover the image or an image
///         of more than `kMaxBackgroundChannels` channels; `ErrorCode::OutOfRange` for a box side
///         outside `[kMinBackgroundSampleExtent, kMaxBackgroundSampleExtent]`, carrying the side and
///         the limit, or a box that would not lie entirely inside the picture;
///         `ErrorCode::EmptySelection` when the validity rule rejects every sample of some channel
///         of the box; or the engine context's refusal of the working memory.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BackgroundSample>
measureImageBackgroundSample(const ExecutionContext& execution, SampleType sampleType, const void* source,
                             const ImageLayout& sourceLayout, const BackgroundSampleRequest& request, std::uint32_t x,
                             std::uint32_t y, const SpatialSelection& selection = {});

/// Measures one hand-placed background site of a checked typed image view.
///
/// The typed spelling of `measureImageBackgroundSample`, which owns every check and every
/// measurement.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param request the box size and the luminance rule.
/// @param x the column the site's centre sits on.
/// @param y the row the site's centre sits on.
/// @param selection which samples mean something.
/// @return whatever `measureImageBackgroundSample` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<BackgroundSample> measureBackgroundSample(const ExecutionContext& execution,
                                                               const ImageView<const SampleT>& source,
                                                               const BackgroundSampleRequest& request, std::uint32_t x,
                                                               std::uint32_t y, const SpatialSelection& selection = {})
{
    return measureImageBackgroundSample(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), request, x,
                                        y, selection);
}

/// Which site of an array is closest to a position.
///
/// The half of "remove the sample nearest where the person clicked" that needs the engine's
/// definition of a site, with the removal itself left to whatever owns the array. Distance is
/// Euclidean in pixels and ties go to the earlier site, so the answer does not depend on the order
/// a search happened to run in.
///
/// Thread-safe, allocation-free, and takes no execution.
///
/// @param sites the array to search.
/// @param x the column to measure from.
/// @param y the row to measure from.
/// @return the index of the closest site, or `ErrorCode::EmptySelection` for an empty array, or
///         `ErrorCode::InvalidArgument` for a non-finite position.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t>
nearestBackgroundSample(std::span<const BackgroundSample> sites, double x, double y);

/// Which surface a background model is.
enum class BackgroundModelKind : std::uint8_t
{
    /// A polynomial of a stated total degree in the two normalised image coordinates.
    ///
    /// Fitted by least squares over the sites, which means it does not pass through any of them and
    /// is therefore the right choice when the sites are noisy or when the illumination really is a
    /// low-order surface.
    Polynomial = 0u,

    /// A thin-plate spline centred on the sites themselves.
    ///
    /// Comes closest to interpolating the sites when the smoothing is at its lowest and relaxes
    /// towards a plane as it rises, which is what a strongly structured gradient - an aircraft
    /// trail, a corner glow - needs and a polynomial cannot represent. Distances are measured in the
    /// normalised coordinates both surfaces use, so on a picture that is not square a pixel along
    /// the longer axis counts for less than one along the shorter.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RadialBasis = 1u
};

/// Whether a value names a model this build implements.
///
/// @param kind the value to inspect.
/// @return true for both surfaces.
[[nodiscard]] constexpr bool isBackgroundModelKind(BackgroundModelKind kind) noexcept
{
    return kind == BackgroundModelKind::Polynomial || kind == BackgroundModelKind::RadialBasis;
}

/// The name of one model, for a diagnostic a person reads.
///
/// @param kind which model.
/// @return a static string, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* backgroundModelKindName(BackgroundModelKind kind) noexcept;

/// What a caller asks a background fit for.
struct BackgroundFitRequest
{
    /// Which surface to fit.
    BackgroundModelKind kind = BackgroundModelKind::Polynomial;

    /// The total degree of the polynomial, read by that model alone.
    ///
    /// Must be within `[1, kMaxBackgroundPolynomialDegree]`. A degree of one is a plane.
    std::uint32_t degree = 2u;

    /// How far the thin-plate spline is allowed to depart from the sites, within `[0, 1]`, read by
    /// that model alone but checked whichever model is asked for.
    ///
    /// The number is mapped onto the ridge added to the system's diagonal, which is
    /// `1e-4 * 10^(3 * (smoothing - 0.5))` times the mean row sum of the thin-plate kernel matrix.
    /// So the ridge stands in the same proportion to the kernel's own scale whatever the frame's
    /// units. It is never zero: zero adds the smallest ridge and comes
    /// closest to interpolating the sites, and one adds a thousand times more and relaxes furthest
    /// towards the plane that best fits them.
    double smoothing = 0.5;
};

/// How many coefficients one fit produces.
///
/// The number a caller sizes its coefficient array with, answered before any site is read.
/// Thread-safe, allocation-free, and takes no execution.
///
/// @param request the model and its parameters.
/// @param siteCount how many sites the fit will run over.
/// @return the count, which is `(degree + 1) * (degree + 2) / 2` for a polynomial and
///         `siteCount + 3` for a thin-plate spline, whose last three coefficients are its affine
///         part - the constant, then the horizontal and vertical terms; `ErrorCode::InvalidArgument`
///         for a model outside the enumeration, a smoothing that is not finite or lies outside the
///         unit interval whichever model is asked for, or a polynomial degree of zero; or
///         `ErrorCode::OutOfRange` for a site count above `kMaxBackgroundSites`, a polynomial degree
///         above `kMaxBackgroundPolynomialDegree`, or a thin-plate site count above
///         `kMaxRadialBasisSites`, each carrying the value and the limit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t> backgroundModelTermCount(const BackgroundFitRequest& request,
                                                                                  std::uint32_t siteCount);

/// What one fit did, and how well it did it.
struct BackgroundFitResult
{
    /// How many coefficients were written.
    std::uint32_t termCount = 0u;

    /// How many sites the fit read.
    std::uint32_t siteCount = 0u;

    /// How many polynomial terms the solve had to drop because the sites do not determine them.
    ///
    /// Zero for a fit whose sites span the requested degree. A non-zero count is the honest warning
    /// that the model is of a lower order than it was asked for - which is what happens when every
    /// site sits on one line, or when a high degree is asked of a handful of sites. Always zero for
    /// a thin-plate spline, whose singular system is refused instead.
    std::uint32_t droppedTerms = 0u;

    /// The root of the mean squared difference between the sites' medians and the fitted surface,
    /// in the image's own units.
    double residualRootMeanSquare = 0.0;

    /// The largest such difference at any one site, in the same units.
    ///
    /// A number far above the root mean square says one site is an object the sampling threshold
    /// let through rather than a background.
    double largestResidual = 0.0;

    /// The ratio of the largest to the smallest diagonal entry of the triangular factor a polynomial
    /// solve kept, which is the square root of the ratio of its largest to its smallest pivot.
    ///
    /// A conditioning number rather than a residual: it says how much a small change in the sites
    /// could move the surface, which is what separates a fit that is merely imperfect from one that
    /// is meaningless. It is a finite number even for a system a term had to be dropped from,
    /// because the dropped term takes no part in it. A thin-plate fit reports zero, because its
    /// elimination measures no conditioning.
    double conditionEstimate = 0.0;
};

/// Fits a stated surface to one channel of a set of sites.
///
/// The arithmetic is Float64 throughout and that is one of the six approved reasons rather than an
/// oversight: a degree-six polynomial in two variables meets twelfth powers of a coordinate when
/// it forms its normal quantities, and the thin-plate spline's system is solved by elimination,
/// both of which lose more significant digits than Float32 has to give. `Image/ComputePrecision.h`
/// names the rule; this is an operation on a few thousand numbers, so the width costs nothing.
///
/// Both surfaces are expressed in coordinates normalised so that the first pixel centre of each
/// axis is -1 and the last is 1, rather than in pixels, because a fit in raw pixel coordinates forms
/// fourth powers of numbers in the thousands and is badly conditioned. The
/// polynomial is fitted by least squares through normal equations and a Cholesky factorisation that
/// drops the terms the sites do not determine; the thin-plate system, with all three affine terms
/// constrained, is solved by elimination with partial pivoting.
///
/// Thread-safe with respect to `execution` and `sites`, which are only read. Runs on the calling
/// thread and checks cancellation at every site and at every row of the thin-plate system as it is
/// built and solved. Failure-atomic with respect to `coefficients`, which is written only by a
/// solve that succeeded.
///
/// @param execution the operation policy and engine-context lease.
/// @param request the model and its parameters.
/// @param sites the measured sites, of which every one is used.
/// @param channel which channel's median to fit, below each site's channel count.
/// @param width how many pixels one row of the picture holds, which sets the coordinate
///        normalisation. Must not be zero.
/// @param height how many rows the picture holds. Must not be zero.
/// @param coefficients where the surface goes, of exactly `backgroundModelTermCount`'s count.
/// @return what the fit did; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one; whatever
///         `backgroundModelTermCount` refuses; `ErrorCode::InvalidArgument` for a zero extent, a
///         coefficient array of the wrong length, or a site whose position or median for the channel
///         is not finite; `ErrorCode::EmptySelection` for no sites at all; `ErrorCode::OutOfRange`
///         when a site carries no median for the channel, carrying the channel and that site's
///         channel count; `ErrorCode::InvalidData` for a thin-plate system that is singular, as it
///         is when the sites all lie on one line, for a polynomial system that determines no term,
///         or for a ridge, coefficient or residual that is not finite; or the engine context's
///         refusal of the working memory.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BackgroundFitResult>
fitBackgroundModel(const ExecutionContext& execution, const BackgroundFitRequest& request,
                   std::span<const BackgroundSample> sites, std::uint32_t channel, std::uint32_t width,
                   std::uint32_t height, std::span<double> coefficients);

/// One fitted surface, everything needed to evaluate it, borrowed for a call.
///
/// The value that passes between the fit and everything downstream. It borrows the sites and the
/// coefficients rather than owning them, so a caller may keep one fit and evaluate it over a
/// hundred frames without a copy; both spans must outlive every call this is passed to.
struct BackgroundModelView
{
    /// The model the coefficients belong to, which must be the one they were fitted with.
    BackgroundFitRequest fit{};

    /// The sites, which a thin-plate spline needs as its centres.
    ///
    /// A polynomial's surface does not read them, but a correction under
    /// `BackgroundLevel::SampleMean` takes its level from the first model's sites whatever that
    /// model is.
    std::span<const BackgroundSample> sites{};

    /// The coefficients, of exactly `backgroundModelTermCount`'s count.
    std::span<const double> coefficients{};

    /// How many pixels one row of the picture the model describes holds.
    std::uint32_t width = 0u;

    /// How many rows that picture holds.
    std::uint32_t height = 0u;
};

/// What one model evaluation did.
struct BackgroundEvaluationResult
{
    /// How many samples of the plane were written, which is its whole extent.
    std::uint64_t samplesWritten = 0u;

    /// The lattice the model was evaluated on, in pixels; one means every pixel.
    std::uint32_t evaluationStride = 0u;

    /// The smallest value written into the plane, in the image's own units.
    double lowest = 0.0;

    /// The largest value written into the plane.
    double highest = 0.0;

    /// Context memory held during the call and released before it returned, in bytes.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// The width the arithmetic ran at, which evaluation always reports as
    /// `ComputePrecision::Float64`.
    ComputePrecision precision = ComputePrecision::Float64;
};

/// Draws a fitted surface into a caller's plane.
///
/// What a caller asks for when it wants to see the gradient rather than remove it, or to write it
/// out beside the corrected frame. The correction itself does not need this: `applyImageBackground`
/// evaluates the same surface as it goes and materialises no plane of it.
///
/// A stride above one evaluates the surface on a lattice of that spacing and fills between the
/// lattice points by bilinear interpolation. For a thin-plate spline whose cost is one term per
/// site per pixel that is the difference between a second and a minute, and for a surface this
/// smooth the interpolation error is far below anything downstream can see. A stride of one
/// evaluates every pixel and interpolates nothing.
///
/// Thread-safe with respect to `execution` and `model`, which are only read. Runs on the threads the
/// operation was admitted and checks cancellation at row-block boundaries.
///
/// @param execution the operation policy and engine-context lease.
/// @param model the fitted surface and the picture it describes.
/// @param destination the plane the surface goes into, of exactly the model's picture.
/// @param evaluationStride the lattice spacing, within `[1, kMaxBackgroundEvaluationStride]`.
/// @return what the evaluation did; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one; whatever
///         `backgroundModelTermCount` refuses for the model's request and site count;
///         `ErrorCode::InvalidArgument` for a zero extent, a coefficient count that is not the one
///         the request implies, a coefficient that is not finite, or a stride outside
///         `[1, kMaxBackgroundEvaluationStride]`; `ErrorCode::EmptySelection` for a thin-plate
///         model with no sites; `ErrorCode::IncompatibleArguments` when the plane is not the model's
///         picture; or the engine context's refusal of the working memory.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BackgroundEvaluationResult>
evaluateBackgroundModel(const ExecutionContext& execution, const BackgroundModelView& model,
                        PlaneView<float> destination, std::uint32_t evaluationStride = 1u);

/// How the modelled background is removed from the frame.
enum class BackgroundCorrection : std::uint8_t
{
    /// The surface is subtracted and the level added back, which is what an additive illumination -
    /// light pollution, sky glow - is.
    Subtract = 0u,

    /// The frame is divided by the surface and multiplied by the level, which is what a
    /// multiplicative one - a vignette, a residual flat error - is.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Divide = 1u
};

/// Whether a value names a correction this build implements.
///
/// @param correction the value to inspect.
/// @return true for both corrections.
[[nodiscard]] constexpr bool isBackgroundCorrection(BackgroundCorrection correction) noexcept
{
    return correction == BackgroundCorrection::Subtract || correction == BackgroundCorrection::Divide;
}

/// Which number the removed background is replaced by.
///
/// A correction that only removed the surface would leave a frame centred on zero, which is not
/// what a background extraction means: the sky is still there, it is simply flat now. So a level is
/// put back, and which number that is has to be stated, because the four possible answers differ by
/// enough to change what the frame looks like.
enum class BackgroundLevel : std::uint8_t
{
    /// The median of the fitted surface over the picture, taken per channel over a lattice of at
    /// most `kBackgroundLevelLatticeExtent` points a side.
    ///
    /// The level the frame actually had, so the corrected frame keeps the brightness it came in with.
    ModelMedian = 0u,

    /// The mean of every median, over every channel, of the sites of the first model a correction
    /// is given, shared by all channels; zero when that model carries no sites.
    ///
    /// Being shared is the point: adding one number to every channel cannot move a colour, while three
    /// per-channel numbers can.
    SampleMean = 1u,

    /// The mean of the source channel itself, taken per channel over the samples the validity rule
    /// admits.
    ///
    /// What keeps a divided frame at the exposure it had rather than at one.
    SourceMean = 2u,

    /// The number the request states, shared by all channels.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Stated = 3u
};

/// Whether a value names a level rule this build implements.
///
/// @param level the value to inspect.
/// @return true for the four rules.
[[nodiscard]] constexpr bool isBackgroundLevel(BackgroundLevel level) noexcept
{
    return level <= BackgroundLevel::Stated;
}

/// The name of one level rule, for a diagnostic a person reads.
///
/// @param level which rule.
/// @return a static string, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* backgroundLevelName(BackgroundLevel level) noexcept;

/// What a caller asks a background correction for.
struct BackgroundApplyRequest
{
    /// Whether the surface is taken away or divided out.
    BackgroundCorrection correction = BackgroundCorrection::Subtract;

    /// Which number replaces the removed background.
    BackgroundLevel level = BackgroundLevel::ModelMedian;

    /// The level itself, read only by `BackgroundLevel::Stated`. Must be finite.
    double statedLevel = 0.0;

    /// The smallest number a division is allowed to divide by, in the image's own units.
    ///
    /// A fitted surface can pass through zero where the frame's illumination did not, and dividing
    /// by it would turn a smooth model into a set of poles. Must be finite and above zero, and read
    /// only by `BackgroundCorrection::Divide`.
    double guard = 1.0e-6;

    /// The lattice the surface is evaluated on during the correction, in pixels.
    ///
    /// Within `[1, kMaxBackgroundEvaluationStride]`, and the same decision
    /// `evaluateBackgroundModel` documents.
    std::uint32_t evaluationStride = 1u;

    /// The width of the deterministic dither added to each sample before it is corrected, in the
    /// image's own units, or zero for none.
    ///
    /// A smooth surface removed from a quantised frame leaves the quantisation behind as visible
    /// bands, and a fraction of a code value of noise breaks them. It is deterministic - derived
    /// from the sample's own position rather than from a generator - so a correction is
    /// reproducible and does not depend on how the work was divided between threads. Must be finite
    /// and not negative, and is stated in the image's units rather than in a normalised interval,
    /// so a UInt16 frame asks for a few code values and a Float32 one for a few millionths.
    ///
    /// Each sample receives this width times a fraction in `[-0.5, 0.5)`, centred on zero
    /// so the dither does not systematically raise the corrected background.
    double dither = 0.0;

    /// The closed interval a corrected value is held inside before it is written, or no value to
    /// write what the arithmetic produced.
    ///
    /// The interval is stated in the image's own units, or not stated at all.
    std::optional<KernelValueWindow> clamp{};

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// What one background correction did.
struct BackgroundApplyResult
{
    /// Pixel selection and writes, summed over the corrected channels.
    KernelRunResult run{};

    /// How many channels were corrected.
    std::uint32_t channelCount = 0u;

    /// The level actually put back, one entry per channel and the rest left at zero.
    std::array<double, kMaxBackgroundChannels> appliedLevel{};

    /// How many stored samples the operation actually moved.
    std::uint64_t samplesChanged = 0u;

    /// How many divisions met the guard rather than the surface.
    ///
    /// The number that says the model went where the frame did not. Zero for every subtraction.
    std::uint64_t guardedSamples = 0u;

    /// How many corrected values a stated clamp had to move.
    std::uint64_t clampedSamples = 0u;

    /// The lattice the surface was evaluated on, in pixels.
    std::uint32_t evaluationStride = 0u;

    /// How many times the operation walked the whole image.
    ///
    /// Two for a level the source itself decides, one otherwise.
    std::uint64_t imagePasses = 0u;

    /// The most context memory held at once during the call, all of it released before the call
    /// returned, in bytes.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// The width the samples travelled at, which this operation always reports as
    /// `ComputePrecision::Float32`.
    ///
    /// The surface itself is evaluated in Float64, as it is fitted; the field describes the working
    /// plane a sample is read into and written out of, which is what the promise in
    /// `SpatialOutputPolicy::contract` is checked against.
    ComputePrecision precision = ComputePrecision::Float32;
};

/// Removes a fitted background from an image, evaluating the surface as it goes.
///
/// The untyped entry point, and the one the exported library carries. One model per channel, in
/// channel order; each channel is corrected by its own surface and by the level the request's rule
/// chooses. No plane of the surface is materialised at full resolution at any point, which is what
/// separates this from evaluating a model and subtracting two images: the difference is the
/// picture's worth of memory and one pass over it. The samples are another matter: an image whose
/// channel is not a Float32 plane with one sample per pixel is read through a Float32 working plane
/// of its extent, and written through another, taken from the engine context.
///
/// Each corrected value is computed in Float64 and narrowed to Float32; an image stored in another
/// representation then receives it through the request's write-back conversion.
///
/// A sample the validity rule rejects leaves its destination sample untouched, and no rejected
/// sample takes part in a level the source decides, so a correction can neither create nor destroy
/// meaninglessness.
///
/// Thread-safe with respect to `execution`, the source, the models and the selection, all of which
/// are only read. Runs on the threads the operation was admitted, checks cancellation at row-block
/// boundaries, and is not failure-atomic.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the frame's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the corrected samples go. Must not be null. It may be the source
///        exactly, because the correction reads one sample to write the same one.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param request the correction, the level rule, the guard, the lattice and the output policy.
/// @param models one fitted surface per channel, in channel order. Under
///        `BackgroundLevel::SampleMean` the first model's sites also decide the level.
/// @param selection which samples mean something.
/// @return what the correction did; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one;
///         `ErrorCode::InvalidArgument` for a representation this build does not know, a null
///         buffer, a layout whose element size is not the representation's, an explicit-mask rule
///         without its mask or a mask given to a rule that reads none, a write-back policy that is
///         malformed or not `DomainMapping::CodeValue`, a correction or level rule outside its
///         enumeration, a guard that is not finite and above zero, a stated level or dither that is
///         not finite, a negative dither, a stride outside `[1, kMaxBackgroundEvaluationStride]`,
///         or a clamp whose endpoints are not finite and in order; `ErrorCode::IncompatibleArguments`
///         for an output promise Float32 arithmetic cannot keep, a mask that does not cover the
///         image, a destination that is not the source's picture, an image of more than
///         `kMaxBackgroundChannels` channels - the result reports a level for at most that many -
///         a model count that is not the channel count, or a model whose picture is not the
///         image's; `ErrorCode::Aliasing` for
///         buffers that overlap without being the same; the refusals `evaluateBackgroundModel`
///         documents for a malformed model; `ErrorCode::EmptySelection` when
///         `BackgroundLevel::SourceMean` finds no sample the validity rule admits; or the engine
///         context's refusal of the working memory.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BackgroundApplyResult>
applyImageBackground(const ExecutionContext& execution, SampleType sampleType, const void* source,
                     const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                     const BackgroundApplyRequest& request, std::span<const BackgroundModelView> models,
                     const SpatialSelection& selection = {});

/// Removes a fitted background from a checked typed image view.
///
/// The typed spelling of `applyImageBackground`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param request the correction, the level rule, the guard, the lattice and the output policy.
/// @param models one fitted surface per channel, in channel order.
/// @param selection which samples mean something.
/// @return whatever `applyImageBackground` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<BackgroundApplyResult>
applyBackground(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                const ImageView<SampleT>& destination, const BackgroundApplyRequest& request,
                std::span<const BackgroundModelView> models, const SpatialSelection& selection = {})
{
    return applyImageBackground(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                                destination.layout(), request, models, selection);
}

/// What a sampling does to the image it reads.
///
/// It writes no image at all - it produces measurements - so it reads samples and changes nothing
/// about the description, the meaning, the solution, the cards or the output decision of anything.
inline constexpr OperationEffect kBackgroundSamplingEffect{.samples = SampleEffect::Reads};

/// What a correction does to the image it writes.
///
/// A background extraction changes samples and nothing else. The extent, the representation and the
/// channel schema are the source's; a frame with its gradient removed is still a frame of the same
/// thing; nothing moves a star, because the correction is per sample; and no source card and no
/// output decision is invalidated by it.
///
/// Validity is preserved, and that is a claim about the arithmetic: a rejected sample takes no part
/// in any level the operation measures and its own destination sample is left untouched, so the
/// operation can neither create nor destroy meaninglessness.
inline constexpr OperationEffect kBackgroundCorrectionEffect{};

} // namespace an
