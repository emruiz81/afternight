// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The numbers an image becomes on its way to an external tool, and the numbers the tool's answer becomes on the
/// way back.
///
/// A neural star-removal or denoising tool runs as a separate program that reads an image file and writes one.
/// Starting it, passing it a file and reading its progress is the application's business; what the image's
/// samples become on the way out, and what the answer's samples become on the way in, is arithmetic, and this
/// header is it. `prepareExternalToolHandoff` writes the image the tool is handed and returns the record of what
/// it decided; `restoreExternalToolAnswer` reads the tool's answer and writes it back in the source's own
/// representation and range through that record.
///
/// **The tool sees the unit interval.** An integer sample is handed as its fraction of full scale. A floating
/// image is mapped from the unit interval widened to its smallest and largest finite valid samples, so an image
/// with samples below zero or above one reaches the tool whole rather than clipped, and its answer is mapped back
/// over the same span: an image by the range's offset and span, a star layer - which the tool composes onto its
/// image by a screen rather than an addition - by the span alone.
///
/// **An optional automatic pre-stretch.** A linear image looks black to a tool trained on stretched pictures, so
/// the hand-off may apply a midtones transfer derived from each channel's exact median and median absolute
/// deviation, per channel or linked, with the conventional clipping of minus 2.8 deviations and target background
/// of a quarter. The answer is brought back through the transfer's inverse, which is the transfer with the
/// complementary midtones balance between the same endpoints.
///
/// **What the tool is handed is stated.** Unsigned sixteen-bit codes, rounded to nearest with ties to even, for a
/// tool that refuses floating-point images; Float32 for one that takes them, which loses nothing.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What an image is. Defined in `Image/ImageDescriptor.h`.
class ImageDescriptor;

/// What a tool is handed.
enum class ExternalToolFormat : std::uint8_t
{
    /// Unsigned sixteen-bit codes of the unit interval, rounded to nearest with ties to even: what a tool that
    /// refuses floating-point images takes.
    UInt16 = 0u,

    /// Float32 samples of the unit interval, for a tool that takes them.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Float32 = 1u
};

/// Whether a value is one of the formats this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param format the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isExternalToolFormat(ExternalToolFormat format) noexcept
{
    return static_cast<std::uint8_t>(format) <= static_cast<std::uint8_t>(ExternalToolFormat::Float32);
}

/// Whether, and how, an image is stretched before a tool sees it.
enum class ExternalToolPreStretch : std::uint8_t
{
    /// As it is.
    None = 0u,

    /// One automatic midtones transfer per channel, from that channel's statistics.
    Unlinked = 1u,

    /// One automatic midtones transfer for every channel, from all of their statistics.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Linked = 2u
};

/// Whether a value is one of the pre-stretches this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param preStretch the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isExternalToolPreStretch(ExternalToolPreStretch preStretch) noexcept
{
    return static_cast<std::uint8_t>(preStretch) <= static_cast<std::uint8_t>(ExternalToolPreStretch::Linked);
}

/// What a tool's answer is, which decides how it is mapped back from the unit interval.
enum class ExternalToolLayer : std::uint8_t
{
    /// An image in the source's place: mapped back by the range's offset and span.
    Image = 0u,

    /// A star layer the tool composes onto its image by a screen: mapped back by the span alone.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    StarLayer = 1u
};

/// Whether a value is one of the layers this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param layer the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isExternalToolLayer(ExternalToolLayer layer) noexcept
{
    return static_cast<std::uint8_t>(layer) <= static_cast<std::uint8_t>(ExternalToolLayer::StarLayer);
}

/// Everything a hand-off does, stated once.
///
/// A plain aggregate whose defaults hand a tool sixteen-bit codes with no stretch.
struct ExternalToolHandoffRequest
{
    /// What the tool is handed.
    ExternalToolFormat format = ExternalToolFormat::UInt16;

    /// Whether the image is stretched first.
    ExternalToolPreStretch preStretch = ExternalToolPreStretch::None;

    /// The shadow clipping of the automatic stretch, in normal deviations; finite. Read by a stretch alone.
    double shadowsClipping = -2.8;

    /// The background the automatic stretch maps the median to, within the unit interval. Read by a stretch alone.
    double targetBackground = 0.25;
};

/// One image a hand-off reads: the source, or the tool's answer.
///
/// Every member is borrowed for the call. The descriptor states the representation, the extent and the channels;
/// an integer is read as its fraction of full scale whatever its numeric domain says, because a tool takes a
/// picture rather than a measurement, and a floating sample is read as it is.
struct ExternalToolSource
{
    /// What the samples are. Must not be null.
    const ImageDescriptor* descriptor = nullptr;

    /// The first byte of the samples, aligned for their representation. Must not be null.
    const void* samples = nullptr;

    /// Where the samples sit; it must describe the descriptor. Must not be null.
    const ImageLayout* layout = nullptr;

    /// Which samples mean something. A mask with one plane per channel gives each channel its own plane; any other
    /// mask gives every channel the view's `maskPlane`.
    KernelValidityView validity{};
};

/// A checked hand-off.
///
/// Built only by `planExternalToolHandoff`, so holding one means the request was accepted. Copyable; it holds the
/// request and nothing else.
class ExternalToolHandoffPlan
{
public:
    /// What was asked for.
    ///
    /// @return the checked request.
    [[nodiscard]] const ExternalToolHandoffRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Records a checked request.
    ///
    /// @param request the request `planExternalToolHandoff` accepted.
    explicit ExternalToolHandoffPlan(const ExternalToolHandoffRequest& request) noexcept : m_request(request) {}

    /// The planner is the only constructor's caller.
    friend AFTERNIGHTENGINE_API Result<ExternalToolHandoffPlan>
    planExternalToolHandoff(const ExternalToolHandoffRequest& request);

    /// The checked request.
    ExternalToolHandoffRequest m_request;
};

/// What one hand-off decided, which bringing the answer back needs.
///
/// A plain record the caller keeps between the two calls; nothing in it refers to memory.
struct ExternalToolHandoff
{
    /// The value handed as zero: zero for an integer source, and at most zero for a floating one.
    double rangeLow = 0.0;

    /// The value handed as one: one for an integer source, and at least one for a floating one.
    double rangeHigh = 1.0;

    /// Whether a transfer was applied.
    bool stretched = false;

    /// The transfer each channel was handed through, the identity where none was; a one-channel image uses the
    /// first.
    std::array<MidtonesTransferParameters, 3u> transfer{};

    /// The source's representation, which a restore writes.
    SampleType sourceSampleType = SampleType::Float32;

    /// The source's width, which an answer must have.
    std::uint32_t width = 0u;

    /// The source's height, which an answer must have.
    std::uint32_t height = 0u;

    /// The source's channel count, one or three, which an answer must have.
    std::uint32_t channels = 1u;

    /// Pixels considered, pixels every sample of which was valid, and samples handed - every sample, an invalid
    /// one as zero.
    KernelRunResult run{};
};

/// What bringing one answer back did.
struct ExternalToolRestoreResult
{
    /// Pixels considered and valid, and samples written.
    KernelRunResult run{};
};

/// Checks a request and makes the plan.
///
/// Thread-safe and allocation-free apart from a failure it describes.
///
/// @param request the format, the pre-stretch and its clipping and target.
/// @return the plan; or `ErrorCode::InvalidArgument` for an enumerator outside its enumeration, a clipping that
///         is not finite or a target outside the unit interval.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ExternalToolHandoffPlan>
planExternalToolHandoff(const ExternalToolHandoffRequest& request);

/// What a source is handed as, as a layout request.
///
/// One plane per channel of the format's element size, the source's width, height and channels, each row starting
/// on an element boundary. The hand-off accepts any layout of that shape, padded or interleaved.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param plan the hand-off.
/// @param source the source's description.
/// @return the request to build the handed image's layout from; or `ErrorCode::IncompatibleArguments` for a
///         source that is not one or three channels.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayoutRequest>
externalToolHandoffLayout(const ExternalToolHandoffPlan& plan, const ImageDescriptor& source);

/// Writes the image a tool is handed and records what was decided.
///
/// The range is measured, every sample is mapped into the unit interval, the automatic transfer - when one is
/// asked for - is derived from the mapped image's exact statistics and applied, and every sample is written once
/// in the format. A sample of a pixel the source's validity rejects is left out of the range and the statistics
/// and handed as zero.
///
/// Thread-safe with respect to the source, which is only read. Runs on the operation's admitted threads, and the
/// answer does not depend on how many. Takes working memory from the execution's engine context. A stop may leave
/// the handed image partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the hand-off.
/// @param source the source: one or three channels of any representation.
/// @param handed the first byte written.
/// @param handedLayout where the handed image goes: the format's element size and the source's extent.
/// @return the record of what was decided; `ErrorCode::NotInitialized` for an empty execution;
///         `ErrorCode::InvalidArgument` for a null member of the source, a layout or pointer that does not
///         describe its descriptor, a null destination, a layout that does not describe the format, or a validity
///         mask missing, unwanted or overlapping the destination; `ErrorCode::IncompatibleArguments` for a source
///         that is not one or three channels, a destination of another extent, one overlapping the source, or a
///         validity mask that does not cover the source; `ErrorCode::InvalidData` for a sample that is not finite
///         in a pixel the validity admits; `ErrorCode::EmptySelection` for a stretch of a channel with no valid
///         sample; the working memory's refusal; or the execution's cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ExternalToolHandoff>
prepareExternalToolHandoff(const ExecutionContext& execution, const ExternalToolHandoffPlan& plan,
                           const ExternalToolSource& source, void* handed, const ImageLayout& handedLayout);

/// Brings a tool's answer back into the source's representation and range.
///
/// Every sample of the answer is read as a fraction of full scale, taken through the inverse of its channel's
/// transfer when the hand-off stretched, mapped back from the unit interval as the layer says, and written once
/// in the source's representation, rounded to nearest with ties to even and saturating. A sample the answer's
/// validity rejects is read as zero.
///
/// Thread-safe with respect to the answer, which is only read. Runs on the operation's admitted threads, and the
/// answer does not depend on how many. Allocation-free apart from a failure it describes. A stop may leave the
/// destination partly written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the hand-off the record came from.
/// @param handoff what `prepareExternalToolHandoff` decided.
/// @param layer whether the answer is an image or a star layer.
/// @param answer the tool's answer: the source's width, height and channels, of any representation.
/// @param destination the first byte written.
/// @param destinationLayout where the result goes: the source's representation and extent.
/// @return what was done; `ErrorCode::NotInitialized` for an empty execution; `ErrorCode::InvalidArgument` for a
///         layer outside its enumeration, a record whose range or transfers are not finite and ordered, a null
///         member of the answer, a layout or pointer that does not describe its descriptor, a null destination, a
///         layout that does not describe the source's representation, or a validity mask missing, unwanted or
///         overlapping the destination; `ErrorCode::IncompatibleArguments` for an answer or destination of
///         another extent than the record's, a destination overlapping the answer, or a validity mask that does
///         not cover the answer; or the execution's cancellation or backend-selection failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ExternalToolRestoreResult>
restoreExternalToolAnswer(const ExecutionContext& execution, const ExternalToolHandoffPlan& plan,
                          const ExternalToolHandoff& handoff, ExternalToolLayer layer, const ExternalToolSource& answer,
                          void* destination, const ImageLayout& destinationLayout);

/// What `prepareExternalToolHandoff` does to the image it writes.
///
/// A new image of another description - the format's representation, stretched and mapped - so the meaning is
/// restated, the source's header cards and output decision do not describe it, and every handed sample is a
/// value. The pixel grid is the source's, so an astrometric solution still describes it.
inline constexpr OperationEffect kExternalToolHandoffEffect{
    .description = DescriptionEffect::Restates,
    .semantics = SemanticEffect::Restates,
    .codecMetadata = CodecMetadataEffect::Drops,
    .outputIntent = OutputIntentEffect::Withdraws,
    .validity = {.policy = ValidityPolicy::Regenerate, .produced = ValidityDescriptor{}}};

/// What `restoreExternalToolAnswer` does to the image it writes.
///
/// The source's description again, but the tool's answer rather than what the source measured, so the meaning is
/// restated; every sample written is a value, and the caller states the source's validity again if it still
/// applies.
inline constexpr OperationEffect kExternalToolRestoreEffect{
    .semantics = SemanticEffect::Restates,
    .validity = {.policy = ValidityPolicy::Regenerate, .produced = ValidityDescriptor{}}};

} // namespace an
