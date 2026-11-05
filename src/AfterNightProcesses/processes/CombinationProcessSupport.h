// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the combination processes share: the metadata a result combined from several images carries, an image a run
/// reads beside its target bound for one of the engine's compositions, and the new image a composition writes into
/// the run's session.
///
/// **A combination is a new image.** Its metadata is built whole by the engine's `combinedImageMetadata` from the
/// images combined and the composition's published effect - one exposure, the facts every source shares, the first
/// source's grid and solution, its lineage with the composition appended - and then carries its processing-state
/// cards: nonlinear under the process's name for most, linear when a channel composition or a narrowband combination
/// keeps linear inputs linear. A copy of the first source's metadata with some keywords edited would state two
/// exposures and keep a source's cards.
///
/// **A recombination is not.** A starless image and its stars are one frame in two parts, so the recombined image
/// keeps the starless image's record - its exposure above all - with the recombination appended to its lineage; only
/// the processing-state cards change.
///
/// **A second image is read under a lease.** The window snapshots the asset of the view a selector names when the run
/// is asked for; the body reads it under a read lease of its own and, for a preview over an area, crops it to the area
/// as the framework cropped the target.
///
/// **A composition is bound, not copied.** The engine's compositions read a descriptor, the first sample, a layout and
/// a validity view; `bindImage` takes them off a leased image. The result is written straight into a new planar image
/// of the run's session, which becomes the run's destination.
///
/// Everything here runs on a run's worker, under its admitted execution, unless it says otherwise.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <QRect>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace process_utils
{

/// What a combination's result says of its linearity on the processing-state cards.
enum class CombinationLinearity : std::uint8_t
{
    /// The result has been through a nonlinear process: the cards say so under the process's name, and the result
    /// claims no linear encoding.
    Nonlinear,

    /// The result is linear under the process's name: a composition of linear inputs that changed none of them.
    Linear,

    /// Linear when every source declares itself linear, nonlinear when any declares itself nonlinear, and otherwise
    /// no card at all: what a channel composition and a narrowband combination write.
    AsSources,

    /// No processing-state card is written.
    Unrecorded
};

/// What a combination process says it did, for the result's record.
struct CombinationRecord
{
    /// The name the operation is given on the processing-state cards, `ANOP`: "image-blend",
    /// "lrgb-combination" and so on.
    std::string stateOperation;

    /// The engine operation the process ran, as the engine spells it, for the result's lineage.
    std::string engineOperation;

    /// What the process ran it with, for the lineage.
    std::string detail;

    /// What the cards say of the result's linearity.
    CombinationLinearity linearity = CombinationLinearity::Nonlinear;
};

/// The metadata of an image a process combined from several, built whole by the engine from the sources' metadata
/// and the composition's published effect, with the processing-state cards the record states.
///
/// The first source is the one whose pixel grid the result shares. The result's meaning is the first source's with
/// a validity that says every sample means something - a composition writes every sample - and no linear encoding
/// claimed for a nonlinear result. The result keeps the cards every source states identically, but for the exposure,
/// processing-state and identity cards a combination drops, and the first source's solution and world-coordinate
/// cards while the effect preserves the solution - whatever the effect declares of a single image's cards, which a
/// channel composition and the narrowband compositions withdraw.
///
/// @param sources the metadata of the images combined, the first sharing the result's pixel grid; none null.
/// @param effect the composition's published declaration.
/// @param described what the result is.
/// @param record what the process did.
/// @return the result's metadata; or what the engine's combination or the cards refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageMetadata>
combinedResultMetadata(std::span<const an::ImageMetadata* const> sources, const an::OperationEffect& effect,
                       const an::ImageDescriptor& described, const CombinationRecord& record);

/// The metadata of a starless image put back together with its stars: the starless image's record kept - its
/// exposure, its cards, its solution - with the recombination appended to its lineage and the processing-state cards
/// the record states.
///
/// @param base the starless image's metadata.
/// @param described what the result is.
/// @param record what the process did.
/// @return the result's metadata; or what the lineage or the cards refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageMetadata>
recombinedResultMetadata(const an::ImageMetadata& base, const an::ImageDescriptor& described,
                         const CombinationRecord& record);

/// How a composition reads a floating image that states a physical quantity, as every floating file is read.
enum class PhysicalReading : std::uint8_t
{
    /// In its own domain: a composition that keeps a physical sum unclamped, as the recombinations do.
    AsStated,
    /// As fractions of full scale, as the view shows it: a composition the engine works in a normalized domain, as a
    /// blend, an LRGB combination and every mask is.
    AsFractions
};

/// One image bound for an engine composition: what it is, where its samples are and which of them mean something.
///
/// A view, not an owner: the image it was bound from must outlive it, and it is not copied, because its validity view
/// points into its own mask view.
class AFTERNIGHTPROCESSES_API BoundImage
{
public:
    /// Binds a leased image. An image decoded in sensor codes is always read as fractions of full scale.
    ///
    /// @param source the image, borrowed for the life of the binding.
    /// @param physical how a floating image stating a physical quantity is read.
    /// @return the binding; `ErrorCode::NotInitialized` for an image that holds nothing.
    [[nodiscard]] static an::Result<std::unique_ptr<BoundImage>>
    bind(const an::ui::ProcessSource& source, PhysicalReading physical = PhysicalReading::AsStated);

    /// What the image is.
    [[nodiscard]] const an::ImageDescriptor& descriptor() const noexcept;

    /// Where its samples sit.
    [[nodiscard]] const an::ImageLayout& layout() const noexcept;

    /// Its first sample.
    [[nodiscard]] const void* samples() const noexcept;

    /// Which samples mean something: the image's rule and its packed mask, when it has one.
    [[nodiscard]] an::KernelValidityView validity() const noexcept;

    /// The image's metadata.
    [[nodiscard]] const an::ImageMetadata& metadata() const noexcept;

private:
    BoundImage() = default;

    std::optional<an::ImageDescriptor> m_descriptor{};
    std::optional<an::ImageLayout> m_layout{};
    const void* m_samples = nullptr;
    const an::ImageMetadata* m_metadata = nullptr;
    an::ValidityDescriptor m_rule{};
    std::optional<an::ValidityMaskView<const std::uint8_t>> m_mask{};
};

/// An image a run reads beside its target: the asset the window snapshotted, read under a lease of the run's own,
/// cropped to the part of the target a run over a part reads - the preview area, or the part a frame over what the
/// preview view shows runs over - when the asset is still the whole image.
///
/// Move-only; the lease and any crop live as long as it does.
class AFTERNIGHTPROCESSES_API LeasedImage
{
public:
    /// Reads an asset for a run.
    ///
    /// @param scope the run's admitted execution.
    /// @param asset the asset the window snapshotted.
    /// @param run what the run snapshotted.
    /// @param previewArea the preview's area in the target's pixels, or an empty rectangle for the whole target: what a
    ///        preview's run that does not state its part (`ProcessRun::part`) is cropped to.
    /// @param target what the run's source is, which a crop is made to match.
    /// @return the image; or what the lease or the crop refused.
    [[nodiscard]] static an::Result<LeasedImage> read(const an::ui::ExecutionScope& scope,
                                                      const an::consumer::ImageAsset& asset,
                                                      const an::ui::ProcessRun& run, const QRect& previewArea,
                                                      const an::ImageDescriptor& target);

    LeasedImage(LeasedImage&&) noexcept = default;
    LeasedImage& operator=(LeasedImage&&) noexcept = default;
    LeasedImage(const LeasedImage&) = delete;
    LeasedImage& operator=(const LeasedImage&) = delete;
    ~LeasedImage() = default;

    /// The image, as a process source.
    [[nodiscard]] an::ui::ProcessSource source() const noexcept;

    /// What the image is.
    [[nodiscard]] std::optional<an::ImageDescriptor> descriptor() const;

private:
    LeasedImage() = default;

    std::optional<an::consumer::AssetReadLease> m_lease{};
    std::optional<an::consumer::ImageAsset> m_cropped{};
    std::optional<an::consumer::AssetReadLease> m_croppedLease{};
};

/// Whether two descriptions have the same width and height.
///
/// @param left one.
/// @param right the other.
/// @return true when the extents agree in width and height.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool sameSize(const an::ImageDescriptor& left,
                                                    const an::ImageDescriptor& right) noexcept;

/// What a composition writes: planar samples of a representation, the first sample and the layout.
using CompositionBody = std::function<an::Status(void* samples, const an::ImageLayout& layout)>;

/// A new image of the run's session a composition writes into: `described`, planar, zeroed, carrying `metadata`;
/// the body is run over its samples and the image adopted.
///
/// @param scope the run's admitted execution, whose engine context holds the samples and whose session the asset
///        belongs to.
/// @param described what the image is.
/// @param metadata what is known about it.
/// @param body what writes it.
/// @return the asset; or what allocating, the body or adopting refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
composeImage(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described, an::ImageMetadata metadata,
             const CompositionBody& body);

/// The description of a composition's result: the target's extent, origin and aspect with `channels` channels of
/// `sampleType` in the normalized linear domain, grey for one channel and red, green and blue for three.
///
/// @param target what the result's grid comes from.
/// @param channels one or three.
/// @param sampleType the result's representation.
/// @param limits the limits of the engine context the result is made in.
/// @return the description; `ErrorCode::InvalidArgument` for another channel count or one the limits refuse.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageDescriptor>
compositionDescriptor(const an::ImageDescriptor& target, std::uint32_t channels, an::SampleType sampleType,
                      const an::EngineLimits& limits);

/// Replaces a run's destination by a copy of it carrying other metadata; its samples are unchanged.
///
/// @param scope the run's admitted execution.
/// @param destination the run's result, replaced.
/// @param metadata what the copy carries.
/// @return success; or what allocating, copying or replacing refused. On a failure the destination is unchanged.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status restateMetadata(const an::ui::ExecutionScope& scope,
                                                                 an::ui::ProcessDestination& destination,
                                                                 an::ImageMetadata metadata);

/// A planar floating copy of an image in fractions of full scale - an unsigned integer a fraction of its largest
/// value, a signed one counted from its smallest, a floating sample as it is - in one stated representation, as a new
/// image of the run's session in the normalized linear domain.
///
/// @param scope the run's admitted execution.
/// @param source what is copied.
/// @param sampleType Float32 or Float64.
/// @return the copy; or what converting it refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
unitFloatCopy(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source, an::SampleType sampleType);

/// An image bound for an engine composition in fractions of full scale: a floating image as it is, an integer one
/// through a working copy - an unsigned integer a fraction of its largest value, a signed one counted from its
/// smallest, which the engine's own reading of a signed integer is not. Not movable: the bound samples point into the
/// copy's lease.
struct CompositionSource
{
    /// The working copy, when one was made.
    std::optional<an::consumer::ImageAsset> copy;

    /// The copy's lease, held while the samples are read.
    std::optional<an::consumer::AssetReadLease> lease;

    /// The samples the composition reads.
    std::unique_ptr<BoundImage> bound;
};

/// The floating representation a composition over an image of a representation works in: Float32 or Float64 for an
/// integer image, as `normalizedWorkingType` says, and a floating image's own.
///
/// @param stored the image's representation.
/// @return the working representation.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::SampleType compositionWorkingType(an::SampleType stored) noexcept;

/// Binds a source for a composition in a working representation.
///
/// @param scope the run's admitted execution, whose session a working copy belongs to.
/// @param source what is read; it must outlive the result.
/// @param working Float32 or Float64, the representation an integer source is copied into.
/// @param physical how a floating source stating a physical quantity is read.
/// @return the bound source; or what the copy or the binding refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::unique_ptr<CompositionSource>>
bindForComposition(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source, an::SampleType working,
                   PhysicalReading physical = PhysicalReading::AsStated);

/// How a recombination reads its starless image and its stars: in their own domain when both state a physical
/// quantity, so the sum is kept above one, and as fractions of full scale otherwise, so a floating file meets an
/// integer or normalized image in the one domain the engine adds in.
///
/// @param starless what the starless image is.
/// @param stars what the stars are.
/// @return the reading both are bound under.
[[nodiscard]] AFTERNIGHTPROCESSES_API PhysicalReading recombinationReading(const an::ImageDescriptor& starless,
                                                                           const an::ImageDescriptor& stars) noexcept;

/// Composes an image in a working representation and narrows it into the representation its description states - to
/// the nearest code, a signed integer's counted from its smallest value: the body writes working samples, Float32 or
/// Float64 as `compositionWorkingType` says of the description; a floating description is composed as `composeImage`
/// composes it.
///
/// @param scope the run's admitted execution, whose session the image belongs to.
/// @param described what the image is, in its own representation.
/// @param metadata what it means.
/// @param body what fills the working samples.
/// @return the image, adopted; or what creating, filling or narrowing it refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
composeNarrowed(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described, an::ImageMetadata metadata,
                const CompositionBody& body);

/// The colour reading a composition gives three channels: the source's red, green and blue read as linear for a
/// source that says it is linear and sRGB-encoded otherwise, as the process framework's channel scope reads every
/// colour image.
///
/// @param described what the image is.
/// @return the encoding.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ColorEncoding colourReadingOf(const an::ImageDescriptor& described) noexcept;

} // namespace process_utils
