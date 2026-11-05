// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A mosaic's white balance and full-scale stretch, as operations a caller asks for.
///
/// A camera raw decode here returns a mosaic of black-subtracted sensor codes and does nothing else to
/// it. Choosing a gain per filter from the camera's record, dividing every gain by the smallest, and
/// stretching the result so that the white level lands on a stated full scale are separate, named
/// operations, done only when a caller asks for them.
///
/// `resolveCfaWhiteBalance` says which gains a camera's record - or `measureCfaGreyWorld`'s
/// measurement of the mosaic itself - calls for, and how far a black-subtracted code has to be
/// stretched to reach a stated full scale. `applyCfaChannelGains` is the multiplication, under a
/// stated conversion. A caller that wants a balanced, stretched mosaic composes them after the
/// decode; a caller that wants the codes leaves them alone.
///
/// The automatic estimate sums both greens as one filter where the camera raw library measures them
/// apart, an automatic balance measures the mosaic rather than applying daylight's gains, and a
/// camera balance without as-shot gains falls back to daylight's rather than dividing by zero.

#pragma once

#include <AfterNightEngine/Algorithms/Demosaic/CfaChannels.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/RawCameraMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <limits>
#include <optional>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// Where a mosaic's white-balance gains come from.
enum class CfaWhiteBalanceMode : std::uint8_t
{
    /// Equal gains, so only a stretch, when one is asked for, changes the samples.
    None,

    /// The decoder's daylight multipliers.
    Daylight,

    /// The as-shot multipliers the camera recorded, or daylight's when it recorded none.
    Camera,

    /// A grey-world measurement of the mosaic itself, taken by `measureCfaGreyWorld`, with daylight's
    /// multiplier standing in for a filter the measurement saw no sample of.
    Automatic,

    /// Gains the caller states.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Custom
};

/// Whether a value names a white-balance mode this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param mode the value to inspect.
/// @return true for each of the five modes.
[[nodiscard]] constexpr bool isCfaWhiteBalanceMode(CfaWhiteBalanceMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(CfaWhiteBalanceMode::Custom);
}

/// One number per filter colour.
struct CfaChannelValues
{
    /// The value for the red filter.
    double red = 1.0;

    /// The value for every green filter, both sites of a named block and each of a tabulated one.
    double green = 1.0;

    /// The value for the blue filter.
    double blue = 1.0;

    /// Two sets agree when all three values do.
    ///
    /// @param left one set.
    /// @param right the other.
    /// @return true when every value is equal.
    [[nodiscard]] friend bool operator==(const CfaChannelValues& left, const CfaChannelValues& right) = default;
};

/// Which record the gains a mode resolved to came from.
enum class CfaWhiteBalanceSource : std::uint8_t
{
    /// `CfaWhiteBalanceMode::None`: equal gains.
    Equal,

    /// The daylight multipliers, asked for by name or fallen back to by `CfaWhiteBalanceMode::Camera`.
    Daylight,

    /// The as-shot multipliers.
    CameraAsShot,

    /// The grey-world measurement, with daylight standing in for any filter it saw nothing of.
    Measured,

    /// The caller's gains.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Stated
};

/// How a grey-world measurement divides a mosaic, and what it treats as clipped.
struct CfaGreyWorldRequest
{
    /// Side of the square blocks the mosaic is divided into, counted from its top-left pixel. A block
    /// the right or bottom edge cuts short is measured too. At least one.
    std::uint32_t blockSize = 8u;

    /// The stored value above which a sample is clipped, per filter colour. A block holding one
    /// clipped sample is left out whole, because the rest of it sits beside a saturated source and its
    /// colour is not the scene's. The default clips nothing; `cfaGreyWorldClipLevels` gives the camera
    /// raw library's own levels.
    CfaChannelValues clippedAbove{(std::numeric_limits<double>::max)(), (std::numeric_limits<double>::max)(),
                                  (std::numeric_limits<double>::max)()};
};

/// What a grey-world measurement saw: each filter's measured samples, summed and counted.
struct CfaGreyWorld
{
    /// The sum of the measured samples under each filter.
    CfaChannelValues sums{0.0, 0.0, 0.0};

    /// How many red samples were summed.
    std::uint64_t redSamples = 0u;

    /// How many green samples were summed.
    std::uint64_t greenSamples = 0u;

    /// How many blue samples were summed.
    std::uint64_t blueSamples = 0u;

    /// How many blocks contributed their samples.
    std::uint64_t blocksMeasured = 0u;

    /// How many blocks were left out because they held a clipped or non-finite sample.
    std::uint64_t blocksSkipped = 0u;
};

/// Measures a mosaic's grey world: each filter's samples, summed over the blocks that hold nothing clipped.
///
/// The camera raw library's automatic white balance, over a mosaic whose black is already gone: the
/// same walk over square blocks and the same refusal of a whole block for one clipped sample. A
/// non-finite sample refuses its block the same way, because it is not a measurement of the scene.
/// Every sample is read as the number it is. The answer does not depend on the thread count: blocks
/// are summed in chunks that the work alone decides, and the chunks are added in order.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation.
/// @param samples first byte of the mosaic.
/// @param layout where the samples sit. One channel.
/// @param cfa the mosaic, named or tabulated.
/// @param request the block size and the clip levels.
/// @param selection which samples mean something. A sample the rule refuses is neither summed nor
///        allowed to refuse its block. `produced` must be null, because a measurement writes no image.
/// @return the measurement; `ErrorCode::NotInitialized` for an execution that holds nothing or has
///         no memory resource; `ErrorCode::InvalidArgument` for a null or misaligned buffer, a
///         representation outside the enumeration, a layout that does not describe the
///         representation, a zero block size, a clip level that is not a number, or a mask that
///         contradicts its rule; `ErrorCode::IncompatibleArguments` for an image with no mosaic,
///         more than one channel, a mask that does not cover it or a produced mask;
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when the chunk
///         partials do not fit; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CfaGreyWorld>
measureCfaGreyWorld(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                    const ImageLayout& layout, const CfaDescriptor& cfa, const CfaGreyWorldRequest& request = {},
                    const CfaChannelSelection& selection = {});

/// The clip levels the camera raw library's automatic white balance uses, over a black-subtracted mosaic.
///
/// The library treats a raw sample within `margin` codes of the white level as clipped. A decoded mosaic
/// has already lost each filter's black, so each level here is the white level, less the margin, less
/// the record's black level and that filter's channel offset - the red, first green and blue offsets.
/// A black tile, which `resolveCfaWhiteBalance` folds into its common black, is not subtracted here.
///
/// Thread-safe and allocation-free.
///
/// @param camera the decoder's calibration record.
/// @param margin how far below the white level clipping starts, in sensor codes; the library uses 25.
/// @return the levels; `ErrorCode::InvalidArgument` for a margin that is negative or not a number; or
///         `ErrorCode::InvalidData` when the record states no white level or a level would not be above zero.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CfaChannelValues> cfaGreyWorldClipLevels(const RawCameraMetadata& camera,
                                                                                   double margin = 25.0);

/// What a white balance asks for.
struct CfaWhiteBalanceRequest
{
    /// Where the gains come from.
    CfaWhiteBalanceMode mode = CfaWhiteBalanceMode::None;

    /// The gains `CfaWhiteBalanceMode::Custom` uses, each positive and finite. Ignored by the other modes.
    CfaChannelValues custom{};

    /// The measurement `CfaWhiteBalanceMode::Automatic` uses, borrowed for the call. Required by that
    /// mode and ignored by the others.
    const CfaGreyWorld* measured = nullptr;

    /// The value a black-subtracted code at the white level is stretched to, or empty to leave the codes'
    /// scale alone.
    std::optional<double> fullScaleCode = 65535.0;
};

/// The gains a white balance resolved to, and the stretch that goes with them.
struct CfaWhiteBalance
{
    /// Relative gains, divided so that the smallest is exactly one.
    CfaChannelValues gains{};

    /// Which record the gains came from.
    CfaWhiteBalanceSource source = CfaWhiteBalanceSource::Equal;

    /// The factor that takes a black-subtracted code at the white level to the requested full scale;
    /// exactly one when no stretch was asked for.
    double fullScale = 1.0;

    /// What `applyCfaChannelGains` multiplies each filter's samples by: its gain times the stretch.
    ///
    /// @return the three multipliers.
    [[nodiscard]] constexpr CfaChannelValues multipliers() const noexcept
    {
        return CfaChannelValues{gains.red * fullScale, gains.green * fullScale, gains.blue * fullScale};
    }
};

/// Resolves a white-balance mode against a camera's calibration record.
///
/// The gains are chosen as the mode says and divided by the smallest of the three. The stretch divides
/// the requested full scale by the span between the white level and the common black - the record's black
/// level, plus the smallest of its four channel offsets, plus the smallest entry of its black tile when it
/// has one - which is the black the camera raw library leaves a mosaic's white level measured from.
///
/// A record's multipliers are usable when the first three are positive and finite. The second green is not
/// read: this engine names one green filter.
///
/// Thread-safe and allocation-free.
///
/// @param camera the decoder's calibration record.
/// @param request the mode, its gains or measurement, and the full scale.
/// @return the resolution; `ErrorCode::InvalidArgument` for a mode outside the enumeration, custom gains
///         that are not positive and finite, an automatic request with no measurement or a full scale that
///         is not positive and finite; or `ErrorCode::InvalidData` when the record has no usable
///         multipliers for the mode, the measurement and the record together leave a filter without a
///         gain, or a stretch was asked for and the record has no white level above its black.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CfaWhiteBalance>
resolveCfaWhiteBalance(const RawCameraMetadata& camera, const CfaWhiteBalanceRequest& request);

/// Multiplies each mosaic sample by its filter's multiplier.
///
/// Computed in double precision and written back into the destination representation under
/// `writeBack` - which is where a caller that wants truncated codes asks for `RoundingMode::TowardZero`.
/// For a sixteen-bit mosaic that asks for exactly that, with saturation and every sample valid, the
/// product is taken in fixed point when every multiplier is a multiple of `2^-24` no larger than
/// 8192, which gives the same truncated codes. The positions do not move and the
/// filter array does not change, so the source's validity rule goes on describing the result: a
/// non-finite sample stays non-finite and a zero stays zero. The two rules a scale can break are refused
/// rather than silently broken: a stated closed range, whose bounds describe the unscaled numbers, and
/// the exact-zero rule under a multiplier below one, which could turn a small nonzero code into a zero.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the mosaic.
/// @param sourceLayout where the mosaic samples sit. One channel.
/// @param cfa the mosaic, named or tabulated.
/// @param destination first byte of the disjoint destination.
/// @param destinationLayout where the results sit. One channel, and the source's extent.
/// @param multipliers each filter's multiplier, finite and not negative.
/// @param writeBack how a computed value becomes a stored sample. Its domain mapping must be
///        `DomainMapping::CodeValue`: the products are the numbers to store.
/// @param selection which source samples mean something. `produced` must be null.
/// @return what the operation did, with the source's rule as the output validity and the write-back's
///         counters; `ErrorCode::NotInitialized` for an execution that holds nothing;
///         `ErrorCode::InvalidArgument` for a multiplier that is negative or not finite, a policy this
///         build does not know, a mapping other than code values, a null or misaligned pointer, a
///         representation outside the enumeration, a layout that does not describe it, or a mask that
///         contradicts its rule; `ErrorCode::IncompatibleArguments` for an image with no mosaic,
///         extents or channel counts that disagree, a mask that does not cover the source, a produced
///         mask, a stated range or the exact-zero rule under a multiplier below one;
///         `ErrorCode::Aliasing` when the destination overlaps an input; `ErrorCode::InvalidData` or
///         `ErrorCode::OutOfRange` when a rejecting write-back policy refused a product; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CfaChannelResult>
applyCfaChannelGains(const ExecutionContext& execution, SampleType sampleType, const void* source,
                     const ImageLayout& sourceLayout, const CfaDescriptor& cfa, void* destination,
                     const ImageLayout& destinationLayout, const CfaChannelValues& multipliers,
                     const ConversionPolicy& writeBack = ConversionPolicy{.domain = DomainMapping::CodeValue},
                     const CfaChannelSelection& selection = {});

/// What multiplying a mosaic by its gains does to the image it writes.
///
/// The picture, its representation and its filter array are unchanged, so the description is preserved;
/// what the numbers mean is not - they are white-balanced, stretched values rather than sensor codes - so
/// the meaning is restated. Each sample is computed from itself alone, so its validity is its source's.
inline constexpr OperationEffect kCfaChannelGainsEffect{.samples = SampleEffect::Writes,
                                                        .description = DescriptionEffect::Preserves,
                                                        .semantics = SemanticEffect::Restates,
                                                        .solution = SolutionEffect::Preserves,
                                                        .codecMetadata = CodecMetadataEffect::Preserves,
                                                        .outputIntent = OutputIntentEffect::Preserves,
                                                        .validity = {.policy = ValidityPolicy::Preserve}};

} // namespace an
