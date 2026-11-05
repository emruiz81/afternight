// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Reaching a mosaic's filters without estimating anything.
///
/// Demosaicing guesses the colours a photosite did not record. These three operations do not: they
/// rearrange what was recorded, and they exist because several things a mosaic is used for need the
/// measurements rather than an estimate of them.
///
/// **Splitting** takes the repeating block apart into one plane per position in it. A flat frame is
/// normalised per filter this way, because the filters have different transmission and one gain for
/// all of them would bake the mosaic into the calibration; an integer resample of a mosaic is done
/// this way, because resampling across filters is meaningless. **Joining** is its exact inverse, and
/// the pair round-trips every sample of a picture whose dimensions are a whole number of blocks.
///
/// **Super-pixel** collapses each block to one colour pixel by averaging the sites of each filter
/// inside it. It is not a cheap demosaic - it produces a picture with a fraction of the pixels - and
/// that is the point: every sample of the result was measured through the filter it is reported as,
/// so there is no interpolation to be wrong about. It is what a focus aid, a thumbnail and a sanity
/// check want.
///
/// What all three share with `Demosaic.h`: the mosaic is a validated descriptor with its phase, the
/// destination is a separate image, the work is spread over the operation's own threads, and
/// cancellation is observed rather than promised. What they do not share is a numeric domain - none
/// of them has a threshold, because none of them makes an estimate.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// How many planes a mosaic of this pattern splits into.
///
/// One per position in the repeating block, which is the only decomposition that never mixes two
/// filters. A tabulated mosaic would split into thirty-six, which is a number nothing asks for -
/// the operation that answers the same question for one is the super-pixel below - so it reports
/// zero and `splitCfaBlockPlanes` refuses it by name.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param pattern the mosaic.
/// @return four for each named arrangement; zero for a tabulated mosaic, for an image with no
///         mosaic, and for a value outside the enumeration.
[[nodiscard]] constexpr std::uint32_t cfaBlockPlaneCount(CfaPattern pattern) noexcept
{
    if (!isCfaPattern(pattern) || pattern == CfaPattern::None || cfaPatternIsTabulated(pattern))
    {
        return 0u;
    }
    const std::uint32_t block = cfaPatternBlockSize(pattern);
    return block * block;
}

/// How many pixels of a mosaic one super-pixel of this pattern is made from, in each direction.
///
/// Two for a named arrangement, whose block holds one red, two green and one blue site. Three for a
/// tabulated mosaic, whose six-by-six block divides into four three-by-three blocks that each hold
/// sites of all three filters - which `planCfaSuperPixel` checks of the actual table rather than
/// assuming.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param pattern the mosaic.
/// @return the block side, or zero for an image with no mosaic and for a value outside the
///         enumeration.
[[nodiscard]] constexpr std::uint32_t cfaSuperPixelBlockSize(CfaPattern pattern) noexcept
{
    if (!isCfaPattern(pattern) || pattern == CfaPattern::None)
    {
        return 0u;
    }
    return cfaPatternIsTabulated(pattern) ? 3u : 2u;
}

/// Which mosaic samples mean something, and where a transformed answer is written.
///
/// A value-based rule needs no mask when samples are merely split or joined: the values do not
/// change, so the same rule still answers for them. An explicit source mask cannot be described by
/// that rule alone and must be rearranged with the samples. A split therefore requires a produced
/// mask with one plane per block position; a join requires one destination plane and accepts either
/// one shared source plane or one per block position. Under a value rule a produced mask is
/// refused, because the rule still describes the rearranged samples.
///
/// A super-pixel combines several samples. Its result is all-valid only when its source is, and an
/// all-valid source must not supply `produced`; otherwise `produced` is required and carries one
/// shared answer or the same answer in three planes. All views are borrowed for one call, and
/// workers may read or write disjoint bits concurrently. A produced mask must not overlap the source
/// mask or either sample image.
struct CfaChannelSelection
{
    /// The value or mask rule deciding whether a source sample means something.
    ValidityDescriptor validity{};

    /// The source's packed bits, required exactly when `validity` names an explicit mask.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;

    /// Where a rearranged or combined explicit answer is written, when the operation requires one.
    ValidityMaskView<std::uint8_t>* produced = nullptr;
};

/// What one split, join or super-pixel did.
struct CfaChannelResult
{
    /// Which mosaic it read.
    CfaPattern pattern = CfaPattern::None;

    /// For a split or a join, the covered samples considered, how many of them mean something, and
    /// the samples written. For a super-pixel, the blocks considered, how many of them had every
    /// site meaningful, and the colour pixels written.
    KernelRunResult run{};

    /// How the result says which samples mean something.
    ValidityDescriptor outputValidity{};

    /// What a super-pixel's write-back conversion met; zero for split and join.
    ConversionCounters conversion{};

    /// How many blocks of the mosaic were covered across.
    std::uint32_t blockColumns = 0u;

    /// How many blocks of the mosaic were covered down.
    std::uint32_t blockRows = 0u;

    /// How many pixels of the mosaic one block covers in each direction.
    std::uint32_t blockSize = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// Context memory held at once, in bytes. None of the three operations takes any, so this is
    /// zero.
    std::uint64_t workingBytes = 0u;

    /// How many mosaic columns were left out because they do not complete a block.
    ///
    /// A picture whose dimensions are not a whole number of blocks has a strip at its right or
    /// bottom edge that cannot be split, joined or averaged. The operation covers what it can and
    /// says how much it left, rather than refusing a frame or reading past its last row.
    std::uint32_t trailingColumns = 0u;

    /// How many mosaic rows were left out because they do not complete a block.
    std::uint32_t trailingRows = 0u;
};

/// What splitting a mosaic does to the image it writes.
///
/// The result is a different picture - a quarter of the pixels, four channels of measurements
/// instead of one of a mosaic - so the description and the meaning are both restated, and an
/// astrometric solution no longer describes its pixel grid. No sample is estimated and none is
/// combined, so the validity of a destination sample is the validity of the one source sample it
/// came from, which is what `ValidityPolicy::Preserve` says.
inline constexpr OperationEffect kCfaSplitEffect{.samples = SampleEffect::Writes,
                                                 .description = DescriptionEffect::Restates,
                                                 .semantics = SemanticEffect::Restates,
                                                 .solution = SolutionEffect::Invalidates,
                                                 .codecMetadata = CodecMetadataEffect::Preserves,
                                                 .outputIntent = OutputIntentEffect::Preserves,
                                                 .validity = {.policy = ValidityPolicy::Preserve}};

/// What a super-pixel does to the image it writes.
///
/// Averaging the like-filtered sites of a block is a combination, so a super-pixel means something
/// exactly when every site it averaged did. The pixel grid shrinks by the block size, so an
/// astrometric solution no longer describes it.
inline constexpr OperationEffect kCfaSuperPixelEffect{.samples = SampleEffect::Writes,
                                                      .description = DescriptionEffect::Restates,
                                                      .semantics = SemanticEffect::Restates,
                                                      .solution = SolutionEffect::Invalidates,
                                                      .codecMetadata = CodecMetadataEffect::Preserves,
                                                      .outputIntent = OutputIntentEffect::Preserves,
                                                      .validity = {.policy = ValidityPolicy::Combine}};

/// Takes a mosaic apart into one plane per position in its repeating block.
///
/// Plane `row * blockSize + column` holds the samples the mosaic carries at that position of every
/// block, in block order, so plane zero of an `Rggb` mosaic with a zero phase is the red sites.
/// Which filter each plane holds is the mosaic's own question and is answered by
/// `CfaDescriptor::colorAt` at the plane's position - the split does not reorder planes to put red
/// first, because a caller that cropped an odd number of columns needs the planes to keep meaning
/// positions rather than colours.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the mosaic.
/// @param sourceLayout where the mosaic samples sit. One channel.
/// @param cfa the mosaic, which must be one of the named arrangements.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where the plane samples sit. `cfaBlockPlaneCount` channels, and an
///        extent of the mosaic's divided by the block size, rounded down.
/// @param selection which source samples mean something, and the transformed mask when one is
///        materialised.
/// @return what the operation did; `ErrorCode::IncompatibleArguments` for an image with no mosaic,
///         a tabulated one, an extent or channel count that does not match, a mask whose extent or
///         plane count does not match, or a produced mask that is absent when needed or present
///         when not; `ErrorCode::InvalidArgument` for a null or misaligned pointer, a representation
///         outside the enumeration, a layout whose element size is not the representation's, or a
///         source mask that contradicts its rule; `ErrorCode::Aliasing` when a written image or mask
///         overlaps an input or another output; `ErrorCode::NotInitialized` for an execution that
///         holds nothing; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the
///         operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CfaChannelResult>
splitCfaBlockPlanes(const ExecutionContext& execution, SampleType sampleType, const void* source,
                    const ImageLayout& sourceLayout, const CfaDescriptor& cfa, void* destination,
                    const ImageLayout& destinationLayout, const CfaChannelSelection& selection = {});

/// Puts a mosaic back together from one plane per position in its repeating block.
///
/// The exact inverse of `splitCfaBlockPlanes` over the part of the picture that is a whole number
/// of blocks. Samples of a trailing partial block are left untouched rather than invented, which is
/// what makes the round trip an identity on the covered region and a no-op outside it.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the plane image.
/// @param sourceLayout where the plane samples sit. `cfaBlockPlaneCount` channels.
/// @param cfa the mosaic being rebuilt, which must be one of the named arrangements.
/// @param destination first byte of the disjoint mosaic.
/// @param destinationLayout where the mosaic samples sit. One channel, and an extent whose whole
///        blocks match the plane image.
/// @param selection which source samples mean something, and the transformed mask when one is
///        materialised.
/// @return what the operation did, with the same failures as the split.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CfaChannelResult>
joinCfaBlockPlanes(const ExecutionContext& execution, SampleType sampleType, const void* source,
                   const ImageLayout& sourceLayout, const CfaDescriptor& cfa, void* destination,
                   const ImageLayout& destinationLayout, const CfaChannelSelection& selection = {});

/// Whether every block of this mosaic holds a site of all three filters.
///
/// Asked of the table and the phase rather than assumed from the pattern. It is true of every named
/// arrangement by construction, and true of every tabulated mosaic this engine has seen - but the
/// table validation does not force it, so a super-pixel over a table that fails it would report a
/// colour it never measured.
///
/// Thread-safe and allocation-free.
///
/// @param cfa the mosaic.
/// @return success when a super-pixel can be formed from every block; or
///         `ErrorCode::IncompatibleArguments` for an image with no mosaic or a table with a block
///         missing a filter.
[[nodiscard]] AFTERNIGHTENGINE_API Status planCfaSuperPixel(const CfaDescriptor& cfa);

/// Collapses each block of a mosaic to one colour pixel by averaging its like-filtered sites.
///
/// Every output sample was measured through the filter it is reported as, so nothing here is an
/// estimate. The two green sites of a named arrangement are averaged rather than one of them being
/// chosen, and the like-filtered sites of a tabulated block are averaged the same way. The sites are
/// summed in double and the mean is rounded to Float32 before the write-back conversion stores it,
/// which is why a representation whose output promise Float32 cannot keep is refused. A block with a
/// meaningless site is still averaged; the produced mask is what marks it.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the mosaic.
/// @param sourceLayout where the mosaic samples sit. One channel.
/// @param cfa the mosaic, which must satisfy `planCfaSuperPixel`.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where the colour samples sit. Three channels, and an extent of the
///        mosaic's divided by `cfaSuperPixelBlockSize`, rounded down.
/// @param output what the result promises and how a computed value becomes a stored sample.
/// @param selection which source samples mean something, and where a combined mask is written.
/// @return what the operation did; the failures of the split, with `ErrorCode::IncompatibleArguments`
///         also for a mosaic `planCfaSuperPixel` refuses; `ErrorCode::InvalidArgument` for an output
///         precision contract outside its enumeration, a conversion policy `checkConversionPolicy`
///         refuses, or a write-back mapping other than `DomainMapping::CodeValue`;
///         `ErrorCode::Unsupported` when the representation cannot keep the output promise at
///         Float32; or `ErrorCode::InvalidData` or `ErrorCode::OutOfRange` when the write-back policy
///         rejects a non-finite or out-of-range mean.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CfaChannelResult>
superPixelCfa(const ExecutionContext& execution, SampleType sampleType, const void* source,
              const ImageLayout& sourceLayout, const CfaDescriptor& cfa, void* destination,
              const ImageLayout& destinationLayout, const SpatialOutputPolicy& output = {},
              const CfaChannelSelection& selection = {});

/// Typed spelling of `splitCfaBlockPlanes`.
///
/// @tparam SampleT one of the eight resident sample types.
/// @param execution the operation policy and engine-context lease.
/// @param source checked one-channel mosaic.
/// @param cfa the mosaic.
/// @param destination checked disjoint plane image.
/// @param selection which samples mean something and where an explicit mask is rearranged.
/// @return whatever `splitCfaBlockPlanes` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<CfaChannelResult>
splitCfaBlocks(const ExecutionContext& execution, const ImageView<const SampleT>& source, const CfaDescriptor& cfa,
               const ImageView<SampleT>& destination, const CfaChannelSelection& selection = {})
{
    return splitCfaBlockPlanes(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), cfa,
                               destination.data(), destination.layout(), selection);
}

/// Typed spelling of `joinCfaBlockPlanes`.
///
/// @tparam SampleT one of the eight resident sample types.
/// @param execution the operation policy and engine-context lease.
/// @param source checked plane image.
/// @param cfa the mosaic being rebuilt.
/// @param destination checked disjoint one-channel mosaic.
/// @param selection which samples mean something and where an explicit mask is rearranged.
/// @return whatever `joinCfaBlockPlanes` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<CfaChannelResult>
joinCfaBlocks(const ExecutionContext& execution, const ImageView<const SampleT>& source, const CfaDescriptor& cfa,
              const ImageView<SampleT>& destination, const CfaChannelSelection& selection = {})
{
    return joinCfaBlockPlanes(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), cfa,
                              destination.data(), destination.layout(), selection);
}

/// Typed spelling of `superPixelCfa`.
///
/// @tparam SampleT one of the eight resident sample types.
/// @param execution the operation policy and engine-context lease.
/// @param source checked one-channel mosaic.
/// @param cfa the mosaic.
/// @param destination checked disjoint three-channel destination.
/// @param output what the result promises and how a computed value becomes a stored sample.
/// @param selection which source samples mean something.
/// @return whatever `superPixelCfa` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<CfaChannelResult>
superPixel(const ExecutionContext& execution, const ImageView<const SampleT>& source, const CfaDescriptor& cfa,
           const ImageView<SampleT>& destination, const SpatialOutputPolicy& output = {},
           const CfaChannelSelection& selection = {})
{
    return superPixelCfa(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), cfa, destination.data(),
                         destination.layout(), output, selection);
}

} // namespace an
