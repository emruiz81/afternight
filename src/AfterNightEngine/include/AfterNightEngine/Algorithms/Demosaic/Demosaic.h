// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Turning one mosaiced sensor plane into three colour planes.
///
/// A colour camera records one number per photosite, under one filter. Demosaicing is the estimate
/// of the two numbers that were not recorded, and it is the first arithmetic almost every colour
/// frame in this engine goes through - so what it assumes about its input is what everything after
/// it inherits.
///
/// This family states each of those assumptions rather than leaving it implicit.
///
/// **The numeric domain is stated, and the clip point with it.** The input is a Float32 working
/// plane, the caller states the sample value that means a fully exposed photosite, and every
/// threshold in every method is derived from that one number, so no clip threshold is applied to
/// samples on a scale it does not describe. Nothing is rescaled on the way in and nothing is
/// quantised.
///
/// **The reach is a property of the method and is written down.** Each method reads a bounded
/// neighbourhood around the pixel it writes, and the outermost pixels have no such neighbourhood.
/// The border they get is one shared rule - the same three-by-three average for every method -
/// rather than each method deciding for itself, so a frame does not change at its edges when the
/// method changes in the middle.
///
/// **A meaningless sample is not interpolated into its neighbours.** The footprint of an output
/// pixel is the method's stated reach in the interior and the shared border kernel's one-pixel reach
/// in the outer band. An output means something exactly when every source sample in that footprint
/// means something. That is deliberately conservative: several adaptive methods choose their taps
/// from the data, so the exact set is not knowable before the arithmetic runs, and a footprint that
/// claimed to be exact would be a guess with a mask around it.
///
/// **The threads are the operation's and the work is tiled.** Every output pixel belongs to exactly
/// one tile and is written once, so the result does not depend on how many workers ran; each tile
/// takes its scratch from one worker-private partition rather than allocating; and cancellation is
/// observed once per tile rather than once per frame.
///
/// **The destination is a separate image.** Three planes come out of one, so there is no in-place
/// spelling of this operation to be tempted by, and an overlapping destination is refused by name.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// Which estimate of the two unrecorded colours an operation makes.
///
/// The methods are not interchangeable and are not ordered by quality. Four are the established
/// Bayer preview and reconstruction estimates, three solve the different six-by-six tabulated
/// mosaic problem, and the final three are specialised Bayer choices for noisy data, four-colour
/// gradients and a luminance-only homogeneity-directed estimate. Which methods apply
/// to which mosaic is `demosaicMethodAppliesTo`, and a plan that pairs them wrongly is refused
/// rather than approximated.
enum class DemosaicMethod : std::uint8_t
{
    /// Linear interpolation of each colour over the sites that recorded it.
    ///
    /// The cheapest estimate there is and the only one with no directional decision at all, which
    /// is exactly what makes it useful: it is the behaviour every other method is measured against
    /// when the question is whether a difference came from the interpolation or from the input.
    Bilinear,

    /// Gradient-weighted green followed by colour-difference red and blue.
    ///
    /// Four directional weights per green estimate, each the reciprocal of a squared local
    /// gradient, and red and blue reconstructed from their difference against the green plane. What
    /// a preview and a registration pass want: enough direction sensitivity to keep a star round,
    /// at a small multiple of the cost of `Bilinear`.
    Fast,

    /// Ratio-corrected demosaicing.
    ///
    /// Chooses between a horizontal and a vertical green estimate from a low-pass discrimination
    /// measure, then corrects each estimate by the local ratio between the colours rather than by
    /// their difference. The default for a frame that will be looked at.
    Rcd,

    /// Aliasing-minimised and zipper-eliminated demosaicing.
    ///
    /// The most expensive of the Bayer methods and the one that holds up on the data this engine
    /// exists for: it interpolates colour differences four ways and chooses among them by their own
    /// variance, switches to area interpolation where a Nyquist-texture test says the mosaic cannot
    /// resolve the detail, reconstructs red and blue along the diagonals, and falls back to a
    /// highlight-safe estimate above the stated clip point.
    Amaze,

    /// A three-by-three weighted average over a tabulated mosaic.
    ///
    /// The preview estimate for a six-by-six mosaic. It has no directional decision and no reach
    /// beyond one pixel, so it is fast and soft, in that order.
    XTransFast,

    /// Markesteijn interpolation over a tabulated mosaic, one pass.
    ///
    /// Estimates green along four directions, builds the full colour for each, and picks per pixel
    /// by a homogeneity measure taken over a luma and two scaled colour differences, or over CIE Lab
    /// when `DemosaicRequest::useCieLab` asks. One pass is the estimate a preview of a six-by-six
    /// mosaic wants.
    MarkesteijnOnePass,

    /// Markesteijn interpolation over a tabulated mosaic, three passes.
    ///
    /// Eight directions rather than four, and the homogeneity measure applied again over a wider
    /// neighbourhood. Several times the cost of one pass and the estimate to stack with.
    MarkesteijnThreePass,

    /// Directional linear minimum mean-square-error interpolation.
    ///
    /// Estimates the green-minus-colour difference independently along each axis, separates local
    /// signal and residual variance over a nine-sample window, and combines the two estimates by
    /// their uncertainty. It is the Bayer choice intended for a noisy short exposure. It works in the
    /// caller's stated linear domain and holds five bounded tile planes rather than complete frame
    /// planes.
    Lmmse,

    /// Variable-number-of-gradients interpolation over a four-colour Bayer working image.
    ///
    /// Retains two distinct green positions while scoring the method's 64 gradient terms in eight
    /// directions, admits a variable set through the local minimum-plus-half-maximum threshold,
    /// and reconstructs chrominance from the selected result. Every output depends only on the halo of
    /// its own tile.
    Vng4,

    /// Luminance-only adaptive homogeneity-directed interpolation.
    ///
    /// Builds horizontal and vertical full-colour candidates, counts thresholded four-neighbour
    /// lightness agreement, and chooses between them over a three-by-three homogeneity map. The name
    /// says luminance because the homogeneity criterion is lightness alone, not the chrominance-aware
    /// criterion of full AHD.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    LuminanceAhd
};

/// Whether a value names a method this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isDemosaicMethod(DemosaicMethod method) noexcept
{
    return static_cast<std::uint8_t>(method) <= static_cast<std::uint8_t>(DemosaicMethod::LuminanceAhd);
}

/// Whether the method describes an estimate for this shape of mosaic.
///
/// The shape rather than the pattern: all four named arrangements are the same two-by-two problem
/// with the filters permuted, and every method written for one works for all four. A tabulated
/// mosaic is a different problem, and no method written for a two-by-two block has anything
/// sensible to do with it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the estimate.
/// @param pattern the mosaic to demosaic.
/// @return true when the method applies. False for `CfaPattern::None`, which has nothing to
///         estimate, and for any pairing of a two-by-two method with a tabulated mosaic or the
///         other way round.
[[nodiscard]] constexpr bool demosaicMethodAppliesTo(DemosaicMethod method, CfaPattern pattern) noexcept
{
    if (!isDemosaicMethod(method) || !isCfaPattern(pattern) || pattern == CfaPattern::None)
    {
        return false;
    }
    const bool methodIsTabulated = method == DemosaicMethod::XTransFast ||
                                   method == DemosaicMethod::MarkesteijnOnePass ||
                                   method == DemosaicMethod::MarkesteijnThreePass;
    return methodIsTabulated == cfaPatternIsTabulated(pattern);
}

/// How far the method reads from the pixel it writes, in pixels.
///
/// The one number the whole family is derived from: it is the halo a tile needs, the width of the
/// border the shared border rule fills, and the footprint an output pixel's validity is combined
/// over. It is stated per method rather than measured, because the reach is a property of the
/// arithmetic and a measured one would be whatever the current implementation happened to touch.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the estimate.
/// @return the reach in pixels, or zero for a value outside the enumeration.
[[nodiscard]] constexpr std::uint32_t demosaicMethodReach(DemosaicMethod method) noexcept
{
    switch (method)
    {
    case DemosaicMethod::Bilinear:
        return 1u;
    case DemosaicMethod::Fast:
        // Three samples out along each axis for the gradient weights, and one more for the
        // colour-difference pass that reads the green estimate of its neighbours.
        return 5u;
    case DemosaicMethod::Rcd:
        // Four samples out for the cardinal gradients, and five more before the outer rings of a
        // tile stop being approximations of themselves: the reconstruction reads its own earlier
        // passes, so a pixel is only final nine in from the edge of what was computed.
        return 9u;
    case DemosaicMethod::Amaze:
        // Sixteen, which is the margin the original mirrors the picture into around every edge tile.
        // This family fills that band with the shared border rule instead, so the number is the
        // width of the band as well as the halo a tile is given.
        return 16u;
    case DemosaicMethod::XTransFast:
        return 1u;
    case DemosaicMethod::MarkesteijnOnePass:
    case DemosaicMethod::MarkesteijnThreePass:
        // Six for the hexagon a green estimate reaches over, and eight more for the chain that
        // measures how much each direction bends and sums that agreement over five-by-five blocks.
        return 14u;
    case DemosaicMethod::Lmmse:
        // A radius-four low pass judged over another radius-four variance window, preceded by the
        // two-pixel directional estimate, one interpolation at green sites and one final
        // colour-difference reconstruction: twelve pixels in all.
        return 12u;
    case DemosaicMethod::Vng4:
        // The four-colour first interpolation reaches one pixel, its direction program reaches two
        // more, and chrominance reconstruction reaches one beyond the final green.
        return 4u;
    case DemosaicMethod::LuminanceAhd:
        // The candidate and homogeneity passes need a five-pixel border.
        return 5u;
    }
    return 0u;
}

/// How many times the method walks the picture.
///
/// The number that says what a method costs before a clock is read, in the same units the spatial
/// family reports it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the estimate.
/// @return the pass count, or zero for a value outside the enumeration.
[[nodiscard]] constexpr std::uint32_t demosaicMethodPasses(DemosaicMethod method) noexcept
{
    switch (method)
    {
    case DemosaicMethod::Bilinear:
    case DemosaicMethod::XTransFast:
        return 1u;
    case DemosaicMethod::Fast:
        return 3u;
    case DemosaicMethod::Rcd:
        return 4u;
    case DemosaicMethod::Amaze:
        return 6u;
    case DemosaicMethod::MarkesteijnOnePass:
        return 4u;
    case DemosaicMethod::MarkesteijnThreePass:
        return 8u;
    case DemosaicMethod::Lmmse:
        return 5u;
    case DemosaicMethod::Vng4:
        return 3u;
    case DemosaicMethod::LuminanceAhd:
        return 4u;
    }
    return 0u;
}

/// The method's own name, for a log line, a diagnostic field or a benchmark manifest.
///
/// The spelling of the enumerator: `DemosaicMethod::Rcd` is `"Rcd"`.
///
/// Thread-safe and allocation-free.
///
/// @param method the method to name. A value outside the enumeration is named
///        `"UnknownDemosaicMethod"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* demosaicMethodName(DemosaicMethod method) noexcept;

/// What the numbers in the mosaic mean.
///
/// Two numbers, so that a threshold expressed as a fraction of full scale is never applied to samples
/// on another scale: the caller says what a fully exposed photosite reads as, in the samples it is
/// passing, and every threshold is derived from that.
///
/// The default describes normalised samples, which is what a calibrated frame in this engine holds.
/// A caller passing raw sixteen-bit codes states `whiteLevel = 65535`.
struct DemosaicDomain
{
    /// The sample value a fully exposed photosite reads as. Finite and above zero, and still so once
    /// narrowed to Float32, the width the estimate computes at.
    double whiteLevel = 1.0;

    /// The fraction of the white level at which a sample stops being a measurement. Above zero and
    /// at most one.
    ///
    /// One means the sensor is trusted to its white level. Below one it is the level a caller knows
    /// the response stops being linear at, and the methods that have a highlight-safe estimate
    /// switch to it there rather than at full scale.
    double clipFraction = 1.0;

    /// The sample value at which a sample stops being a measurement.
    ///
    /// Allocation-free and usable in a constant expression.
    ///
    /// @return the white level scaled by the clip fraction.
    [[nodiscard]] constexpr double clipPoint() const noexcept
    {
        return whiteLevel * clipFraction;
    }

    /// Two domains agree when both numbers agree exactly.
    friend constexpr bool operator==(const DemosaicDomain& left, const DemosaicDomain& right) noexcept = default;
};

/// One demosaic request.
struct DemosaicRequest
{
    /// Which estimate to make. Must apply to the mosaic the plan is built for.
    DemosaicMethod method = DemosaicMethod::Rcd;

    /// What the samples mean.
    DemosaicDomain domain{};

    /// Whether a tabulated method judges its directions in a perceptual space.
    ///
    /// Read by the Markesteijn methods and ignored by every other, because no other has a
    /// homogeneity measure to take in one space or another. The cheaper space - a luma and two
    /// scaled colour differences - is the default; the perceptual one costs a cube root per
    /// direction per pixel and is what the method was described with.
    bool useCieLab = false;

    /// What the result promises and how a computed value becomes a stored sample.
    ///
    /// The spatial family's record, because it is the same two decisions and a second spelling of
    /// them would be free to drift. `DomainMapping::CodeValue` is the only mapping accepted, for
    /// the same reason: the working numbers are the stored numbers, and a demosaic that rescaled
    /// its output would be an estimate and a domain change at once.
    SpatialOutputPolicy output{};
};

/// Which of a mosaic's samples mean something, and where the answer for the result goes.
///
/// The source half is the spatial family's question asked about one plane. The destination half is
/// this family's own, and it exists because the answer cannot be a rule: an output pixel's
/// footprint covers many source samples, so "which outputs mean something" is a per-pixel fact even
/// when the input was described by a rule, and only a mask can carry it.
struct DemosaicSelection
{
    /// The value or mask rule deciding whether a source sample participates.
    ValidityDescriptor validity{};

    /// Full-image packed validity bits over the mosaic, or null when `validity` names no mask.
    ///
    /// One bit-plane, because a mosaic has one channel. Borrowed for one call and read concurrently
    /// by the operation's workers.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;

    /// Where the result's validity is written, or null when the source is all-valid.
    ///
    /// Required exactly when `validity` is anything other than the all-valid rule, and refused
    /// otherwise: an all-valid mosaic produces an all-valid result, and a mask saying so would be
    /// a frame-sized allocation carrying no information. It must cover the destination extent with
    /// either one shared bit-plane or one per destination channel; the operation writes the same
    /// answer to each, because a source sample that means nothing means nothing for all three
    /// colours estimated from it.
    ///
    /// Its bytes must not overlap the source mask or either sample image; the operation writes them
    /// while those inputs may still be read on another worker.
    ValidityMaskView<std::uint8_t>* produced = nullptr;
};

/// A checked demosaic, reusable for every frame of one camera.
///
/// It carries the mosaic as well as the method, because which filter covers which pixel is what
/// every kernel in the family reads, and a plan that did not carry it would have to be paired with
/// a descriptor at each call and could be paired with the wrong one. A crop changes the phase and
/// therefore needs its own plan, which costs nothing: planning allocates nothing and reads no
/// image.
class DemosaicPlan
{
public:
    /// Records an already checked request and mosaic.
    ///
    /// @param request the method, domain and output policy.
    /// @param cfa the mosaic this plan demosaics.
    AFTERNIGHTENGINE_API DemosaicPlan(const DemosaicRequest& request, const CfaDescriptor& cfa) noexcept;

    /// The request from which the plan was derived.
    ///
    /// @return the recorded request.
    [[nodiscard]] AFTERNIGHTENGINE_API const DemosaicRequest& request() const noexcept;

    /// The mosaic this plan demosaics.
    ///
    /// @return the recorded filter array, which is always mosaiced.
    [[nodiscard]] AFTERNIGHTENGINE_API const CfaDescriptor& cfa() const noexcept;

    /// Which estimate this plan makes.
    ///
    /// @return the recorded method.
    [[nodiscard]] AFTERNIGHTENGINE_API DemosaicMethod method() const noexcept;

    /// How far the method reads from the pixel it writes.
    ///
    /// @return the reach in pixels, which is also the width of the border the shared rule fills.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t reach() const noexcept;

    /// How many bytes of worker-private scratch one tile of this plan needs.
    ///
    /// Derived from the method and the tile geometry rather than measured, so the memory an
    /// operation will hold is known before it starts and is charged to the engine context.
    ///
    /// @param tileWidth how many columns the tile writes, before its method-specific halo is added.
    /// @param tileHeight how many rows the tile writes, before its method-specific halo is added.
    /// @return the byte count, which is zero for a method that needs no scratch.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t tileScratchBytes(std::uint32_t tileWidth,
                                                                      std::uint32_t tileHeight) const noexcept;

    /// The versioned numeric contract this plan runs.
    ///
    /// @return the operation and its version, which a conformance run records beside its output.
    [[nodiscard]] AFTERNIGHTENGINE_API KernelId kernelId() const noexcept;

private:
    /// The checked request.
    DemosaicRequest m_request{};

    /// The checked mosaic.
    CfaDescriptor m_cfa{};
};

/// Checks and records a demosaic request against the mosaic it will run on.
///
/// Thread-safe and allocation-free apart from a failure it has to describe.
///
/// The representation is not known yet, so whether it can keep the output policy's precision
/// promise is checked by `demosaicImage`.
///
/// @param request the method, domain and output policy.
/// @param cfa the mosaic to demosaic.
/// @return a reusable plan; `ErrorCode::InvalidArgument` for a method outside the enumeration, a
///         white level that is not finite and above zero, a clip fraction not above zero and at most
///         one, a white level or clip point that is not finite and above zero in Float32, a filter
///         description `CfaDescriptor::check` refuses, an output precision contract outside its
///         enumeration, a conversion policy `checkConversionPolicy` refuses, or a write-back mapping
///         other than `DomainMapping::CodeValue`; or `ErrorCode::IncompatibleArguments` for an image
///         with no mosaic or a method that does not apply to this one.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DemosaicPlan> planDemosaic(const DemosaicRequest& request,
                                                                     const CfaDescriptor& cfa);

/// What one demosaic did.
struct DemosaicResult
{
    /// Which estimate ran.
    DemosaicMethod method = DemosaicMethod::Rcd;

    /// Which mosaic it read.
    CfaPattern pattern = CfaPattern::None;

    /// Destination pixels covered, how many of them mean something, and how many were written.
    ///
    /// The selection is about the *result*: `total` is the destination pixels and `valid` is how
    /// many of them had a wholly meaningful footprint. `outputsWritten` is every destination pixel,
    /// because the arithmetic runs over the whole picture whatever the mosaic claimed - an estimate
    /// made partly from meaningless samples is a number the operation writes and the produced mask
    /// marks, rather than a hole left in the destination for the caller to find later.
    KernelRunResult run{};

    /// How the result says which samples mean something.
    ///
    /// All-valid input produces `ValidityRule::AllValid`; every other input produces
    /// `ValidityRule::ExplicitMask`, carried by `DemosaicSelection::produced`.
    ValidityDescriptor outputValidity{};

    /// What the write-back conversion met across all three destination channels.
    ConversionCounters conversion{};

    /// How many tiles the interior was split into; zero when the whole picture is border band.
    std::uint64_t tileCount = 0u;

    /// Workers the interior's tiles could actually use, which is never above the admitted
    /// concurrency; zero when there is no interior.
    std::uint32_t plannedWorkers = 0u;

    /// How many pixels of the frame edge the shared border rule filled.
    std::uint32_t borderWidth = 0u;

    /// How many times the operation walked the picture.
    std::uint64_t imagePasses = 0u;

    /// Context memory held at once, in bytes, across the working planes and the tile scratch.
    std::uint64_t workingBytes = 0u;

    /// The width the arithmetic ran at, which this family always reports as
    /// `ComputePrecision::Float32`.
    ComputePrecision precision = ComputePrecision::Float32;

    /// Which implementation ran, read back out of the dispatch table rather than from the request.
    BackendSelection backend{};

    /// Whether the mosaic was read where it lay rather than gathered into a working plane.
    bool readInPlace = false;

    /// Whether the three colour planes were written where they lie rather than scattered from
    /// working planes.
    bool wroteInPlace = false;
};

/// What every operation in this family does to the image it writes.
///
/// A demosaic writes a different image from the one it read: three channels where there was one, a
/// resolved filter array where there was a mosaic, and colour where there was intensity. So the
/// description is restated rather than preserved, and so is the meaning. Nothing moves a star, so
/// an astrometric solution still describes the result; the source header cards still describe the
/// exposure that produced it; and no output decision is invalidated by estimating two colours.
///
/// The validity policy is `ValidityPolicy::Combine` and it is a claim about the arithmetic: an
/// output sample is estimated from a neighbourhood of source samples, so it means something exactly
/// when all of them did. That is the one policy whose answer can be a rule none of its inputs had,
/// which is why this family materialises a mask rather than restating a rule. The neighbourhood is
/// the method's stated reach inside the picture and one pixel inside the border band, because those
/// are what the two kernels actually read.
inline constexpr OperationEffect kDemosaicEffect{.samples = SampleEffect::Writes,
                                                 .description = DescriptionEffect::Restates,
                                                 .semantics = SemanticEffect::Restates,
                                                 .solution = SolutionEffect::Preserves,
                                                 .codecMetadata = CodecMetadataEffect::Preserves,
                                                 .outputIntent = OutputIntentEffect::Preserves,
                                                 .validity = {.policy = ValidityPolicy::Combine}};

/// Estimates the two unrecorded colours of every pixel of a mosaic.
///
/// The source is one channel, the destination three, and both describe the same picture. Both are
/// read and written in the representation the layouts state; a Float32 planar image is filtered
/// where it lies and everything else is gathered and scattered, which is a copy the result reports
/// rather than one it hides.
///
/// The picture is split into tiles, each tile is estimated on one of the operation's threads with
/// its own scratch, and each output pixel belongs to exactly one tile - so the result is identical
/// at any thread count. Cancellation is observed before every tile. Progress is reported once per
/// tile against a total of the tile count plus four, the four being the border, the write-back, the
/// validity pass and completion.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the mosaic.
/// @param sourceLayout where the mosaic samples sit. One channel.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where the colour samples sit. Three channels, same extent.
/// @param plan a plan returned by `planDemosaic`.
/// @param selection which source samples mean something, and where the answer goes.
/// @return what the operation did; `ErrorCode::NotInitialized` for an execution that holds nothing;
///         `ErrorCode::InvalidArgument` for a null or misaligned pointer, a representation outside
///         the enumeration, a layout that does not describe the representation, a selection whose
///         mask contradicts its rule, or a plan whose method this build has no kernel for;
///         `ErrorCode::Aliasing` when a written image or mask overlaps an input or another output;
///         `ErrorCode::IncompatibleArguments` for extents, channel counts or mask geometry that
///         disagree, or a produced mask that is absent when needed or present when not;
///         `ErrorCode::Unsupported` when the representation cannot keep the output policy's
///         precision promise at Float32; `ErrorCode::UnsupportedBackend` when a required compiled
///         method is not available; the write-back conversion's `ErrorCode::InvalidData` or
///         `ErrorCode::OutOfRange` refusal; `ErrorCode::MemoryBudgetExceeded` or
///         `ErrorCode::AllocationFailed` when working memory is refused; `ErrorCode::LimitExceeded`
///         when the tile plan's counts or sizes do not fit; or `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped. A picture too small to
///         contain an interior is handled entirely by the shared border rule.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DemosaicResult>
demosaicImage(const ExecutionContext& execution, SampleType sampleType, const void* source,
              const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
              const DemosaicPlan& plan, const DemosaicSelection& selection = {});

/// Typed spelling of `demosaicImage`.
///
/// @tparam SampleT one of the eight resident sample types.
/// @param execution the operation policy and engine-context lease.
/// @param source checked one-channel mosaic.
/// @param destination checked disjoint three-channel destination.
/// @param plan reusable demosaic plan.
/// @param selection which source samples mean something, and where the answer goes.
/// @return whatever `demosaicImage` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<DemosaicResult> demosaic(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                                              const ImageView<SampleT>& destination, const DemosaicPlan& plan,
                                              const DemosaicSelection& selection = {})
{
    return demosaicImage(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                         destination.layout(), plan, selection);
}

} // namespace an
