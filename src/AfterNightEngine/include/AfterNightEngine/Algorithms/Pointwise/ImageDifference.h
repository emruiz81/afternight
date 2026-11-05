// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How far apart two images are, sample by sample, written as an image.
///
/// A comparison view shows where two versions of one picture differ - an image before and after a
/// process, two frames of one target - and what it shows is a third image: the magnitude of the
/// difference at every sample. This header is the operation that computes it, and each of its decisions
/// is stated.
///
/// **What is compared.** Both images have one extent, one channel schema, one colour basis and one
/// numeric domain. They may differ in representation: a sample is read as the number its domain says it
/// is - an integer in a normalized or encoded domain as a fraction of its representation's full scale,
/// the engine's own conversion; an integer sensor code as the code; a floating sample as it is - so a
/// sixteen-bit frame and its Float32 copy compare equal. Two images that state different domains are
/// refused rather than subtracted: the difference between a linear frame and a stretched one measures
/// the stretch, and a caller who wants that number restates one description on purpose.
///
/// **Three measures.** `ImageDifferenceMeasure::Absolute` writes each channel's absolute difference to
/// the same channel. `ImageDifferenceMeasure::MeanAbsolute` writes one channel, the mean over every
/// channel - alpha included - of the absolute differences, which is the grey difference the product's
/// comparison generator offers. `ImageDifferenceMeasure::Luminance` writes one channel, the absolute
/// difference of the two images' luminances, each the weighted sum of its red, green and blue samples
/// under the request's weights; over encoded samples that sum is a luma, and the result says so only by
/// the domain it carries.
///
/// **The result is stated, not inherited.** It is Float32 or Float64 as the request says, planar, in
/// the sources' common numeric domain - nothing is clamped, rounded to an integer code or rescaled - and
/// `imageDifferenceDescriptor` says what it is. Every difference is computed in double and narrowed once.
///
/// **Validity is per sample.** An absolute difference is written where both sources' samples of that
/// channel are valid; a measure that reads every channel of a pixel is written where every one of them
/// is. A sample that is not written is left as the destination held it and counted, and the effect
/// declares `ValidityPolicy::Combine`, so `resolveImageValidity` builds the mask that says which.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorChannels.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// Which difference an image difference writes.
enum class ImageDifferenceMeasure : std::uint8_t
{
    /// Every channel's absolute difference, written to the same channel.
    Absolute = 0u,

    /// One channel: the mean over every channel, alpha included, of the absolute differences.
    MeanAbsolute = 1u,

    /// One channel: the absolute difference of the two weighted sums of red, green and blue. A grey
    /// image's luminance is its one channel; an alpha channel takes no part.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Luminance = 2u
};

/// Whether a value is one of the measures this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param measure the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isImageDifferenceMeasure(ImageDifferenceMeasure measure) noexcept
{
    return static_cast<std::uint8_t>(measure) <= static_cast<std::uint8_t>(ImageDifferenceMeasure::Luminance);
}

/// The weights of the luminance of linear red, green and blue in the sRGB and BT.709 primaries.
inline constexpr ColorChannelValues kRec709LuminanceWeights{0.2126, 0.7152, 0.0722};

/// Everything an image difference does, stated once.
///
/// A plain aggregate whose defaults are the per-channel absolute difference into Float32. The planner
/// checks it, and the plan keeps a copy.
struct ImageDifferenceRequest
{
    /// Which difference is written.
    ImageDifferenceMeasure measure = ImageDifferenceMeasure::Absolute;

    /// The weight of red, green and blue in a luminance. Read by `ImageDifferenceMeasure::Luminance`
    /// alone. Each finite and not negative, and their sum positive; they are used as given, not
    /// normalized.
    ColorChannelValues luminanceWeights = kRec709LuminanceWeights;

    /// The representation written: `SampleType::Float32` or `SampleType::Float64`.
    SampleType output = SampleType::Float32;
};

/// One of the two images a difference reads.
///
/// Every member is borrowed for the call. The descriptor states the representation, the extent, the
/// channels and the numeric domain the samples are read in.
struct ImageDifferenceSource
{
    /// What the samples are. Must not be null.
    const ImageDescriptor* descriptor = nullptr;

    /// The first byte of the samples, aligned for their representation. Must not be null.
    const void* samples = nullptr;

    /// Where the samples sit; it must describe the descriptor. Must not be null.
    const ImageLayout* layout = nullptr;

    /// Which samples mean something. A mask with one plane per channel gives each channel its own
    /// plane; any other mask gives every channel the view's `maskPlane`.
    KernelValidityView validity{};
};

/// What one image difference did.
struct ImageDifferenceResult
{
    /// Samples considered and admitted, and destination samples written. A pixel counts once per
    /// channel it writes: every channel for `ImageDifferenceMeasure::Absolute`, one otherwise.
    KernelRunResult run{};

    /// The largest difference written, or zero when nothing was written. Not a number when a written
    /// difference was.
    double largestDifference = 0.0;
};

/// A checked image difference.
///
/// Built only by `planImageDifference`, so holding one means the request was accepted. Copyable; it
/// holds the request and nothing else.
class ImageDifferencePlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const ImageDifferenceRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planImageDifference` accepted.
    explicit ImageDifferencePlan(const ImageDifferenceRequest& request) noexcept : m_request(request) {}

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<ImageDifferencePlan> planImageDifference(const ImageDifferenceRequest& request);

    /// The checked request.
    ImageDifferenceRequest m_request;
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the measure, the weights and the output representation.
/// @return the plan; or `ErrorCode::InvalidArgument` for a measure outside its enumeration, a weight
///         that is not finite or is negative, weights whose sum is not positive, or an output other than
///         Float32 or Float64.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDifferencePlan>
planImageDifference(const ImageDifferenceRequest& request);

/// What a difference of two images writes, as a description.
///
/// The extent of the sources with every channel for `ImageDifferenceMeasure::Absolute` and one for the
/// other measures; the request's representation; the sources' schema, or `ChannelSchema::Mono` for one
/// channel; the sources' colour basis for an absolute difference and none otherwise; and the sources'
/// numeric domain. Filter array, origin and pixel aspect are the left source's.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the difference.
/// @param left one source's description.
/// @param right the other's.
/// @return the description; or `ErrorCode::IncompatibleArguments` when the two differ in extent, schema,
///         colour basis or numeric domain, or a luminance is asked of a schema that is not grey, RGB or
///         RGBA; or `ErrorCode::InvalidArgument` when the description it would produce is one no image
///         can have.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDescriptor>
imageDifferenceDescriptor(const ImageDifferencePlan& plan, const ImageDescriptor& left, const ImageDescriptor& right);

/// What a difference of two images writes, as a layout request.
///
/// One plane per written channel, rows aligned to the engine's buffer alignment. The executor accepts
/// any planar layout of the same extent, padded or not.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the difference.
/// @param left one source's description, whose extent the result has.
/// @return the request to build the destination's layout from; or `ErrorCode::IncompatibleArguments`
///         when a luminance is asked of a schema that is not grey, RGB or RGBA.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest> imageDifferenceLayout(const ImageDifferencePlan& plan,
                                                                                    const ImageDescriptor& left);

/// Writes the difference of two images.
///
/// Every pixel is visited once and every written sample is computed in double from the two sources'
/// samples as their common domain reads them, then narrowed to the output representation. The
/// destination may not overlap either source or either mask.
///
/// Thread-safe with respect to both sources, which are only read. Runs on the operation's admitted
/// threads, and the answer does not depend on how many. A stop may leave earlier rows written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the difference.
/// @param left the image the right one is subtracted from; the measures are symmetric, so the order
///        decides only whose filter array, origin and aspect the description keeps.
/// @param right the other image.
/// @param destination the first byte written, aligned for the output representation.
/// @param destinationLayout where the result goes: planar, the sources' extent, with the channels
///        `imageDifferenceLayout` states.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a null member of a source, a layout or pointer that does not
///         describe its descriptor, a null or misaligned destination, a destination layout of another
///         representation, or a mask missing, unwanted or overlapping the destination;
///         `ErrorCode::IncompatibleArguments` for sources that differ in extent, schema, colour basis or
///         numeric domain, a luminance of a schema that is not grey, RGB or RGBA, a destination of
///         another extent or arrangement, a destination overlapping a source, or a mask that does not
///         cover its source; or the execution's cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDifferenceResult>
computeImageDifference(const ExecutionContext& execution, const ImageDifferencePlan& plan,
                       const ImageDifferenceSource& left, const ImageDifferenceSource& right, void* destination,
                       const ImageLayout& destinationLayout);

/// What `computeImageDifference` does to the image it writes.
///
/// A new image: its samples are written, its description is the one `imageDifferenceDescriptor`
/// states, and its meaning is restated, because a difference is not the quantity either source
/// measured. The source cards and any output decision described a source rather than the difference,
/// so they are withdrawn; the pixels are where they were, so an astrometric solution still maps them.
/// The validity combines both sources' claims.
inline constexpr OperationEffect kImageDifferenceEffect{.samples = SampleEffect::Writes,
                                                        .description = DescriptionEffect::Restates,
                                                        .semantics = SemanticEffect::Restates,
                                                        .solution = SolutionEffect::Preserves,
                                                        .codecMetadata = CodecMetadataEffect::Drops,
                                                        .outputIntent = OutputIntentEffect::Withdraws,
                                                        .validity =
                                                            ValidityTransition{.policy = ValidityPolicy::Combine}};

} // namespace an
