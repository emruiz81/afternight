// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The six metadata objects travelling as one, and the only way to change them.
///
/// Splitting metadata into six typed objects was the point; putting six of them on every algorithm
/// signature was never part of it. This is the aggregate that carries them, and it is deliberately
/// not a bag: a bag is a map of cards with no schema, which is precisely what makes a source fact
/// and an output decision the same kind of thing. Six named members have a schema, and `OutputIntent`
/// stays a distinct member that nothing else can be mistaken for.
///
/// **An algorithm receives immutable metadata and returns a transition.** It does not clone the
/// aggregate and edit it. It states what it changed - the meaning, the astrometric solution, the
/// decision about how the result is to be written - and the transition is applied by whoever owns
/// the image. What that buys is reviewability: an algorithm that changes what an image means without
/// saying so is a defect the type system can catch, because the only door is this one and it is
/// narrow.
///
/// **The door is narrow in three specific ways, and each one is a rule rather than an omission.**
///
/// The acquisition record cannot be changed at all. There is no field for it here, which is the
/// enforcement: exposure, filter, camera and observation time are facts about where the data came
/// from, and a product whose acquisition facts differ is a new image built from scratch rather than
/// an old one with its history edited.
///
/// A transition that changes anything must record what changed it. A lineage with a gap in it is a
/// lineage that cannot answer the one question it exists for, and requiring the entry is cheaper
/// than auditing for the operations that forgot.
///
/// A transition says which of the two semantic objects it touched, and `changesMeaning` reports it.
/// That is what lets an image move its semantic generation for a change of meaning and not for a
/// corrected object name - the difference between discarding a plate solution and keeping it.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <optional>

namespace an
{

/// Everything known about an image that is not its samples or its geometry.
///
/// Six immutable members. A default-constructed aggregate states nothing at all, which is the
/// correct description of an image the engine has just allocated: no acquisition facts, a freshly
/// decoded meaning, no solution, no lineage, nothing preserved, and - the important one - no
/// decision about how it is to be written.
///
/// Copyable, because a metadata edit produces a second aggregate rather than changing one. The
/// objects are small next to a pixel buffer; that is an assumption stated here rather than a
/// measurement, and the one to revisit if a workflow ever copies metadata in an inner loop.
class ImageMetadata
{
public:
    /// Metadata that states nothing.
    ImageMetadata() = default;

    /// Metadata built from six stated parts.
    ///
    /// @param acquisition what the instrument was doing.
    /// @param semantics what has been done to the samples and under which encoding.
    /// @param astrometry where the image is pointing, if anything has worked that out.
    /// @param provenance where it came from and what has been done to it.
    /// @param codec what the source file said, kept as it said it.
    /// @param outputIntent how it is to be written. Absent unless something has decided; that
    ///        default is what keeps a source fact from becoming an output decision on its own.
    /// @return the aggregate. Nothing is re-validated here: each part was checked when it was
    ///         built, and this does not add a rule that spans them.
    [[nodiscard]] static AFTERNIGHTENGINE_API ImageMetadata create(AcquisitionMetadata acquisition,
                                                                   ImageSemantics semantics,
                                                                   AstrometricSolution astrometry,
                                                                   Provenance provenance, CodecMetadata codec,
                                                                   std::optional<OutputIntent> outputIntent);

    /// What the instrument was doing.
    ///
    /// @return the acquisition record, which may state nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const AcquisitionMetadata& acquisition() const noexcept;

    /// What has been done to the samples and under which encoding.
    ///
    /// @return the processing meaning.
    [[nodiscard]] AFTERNIGHTENGINE_API const ImageSemantics& semantics() const noexcept;

    /// Where the image is pointing.
    ///
    /// @return the solution, which is absent for almost every image.
    [[nodiscard]] AFTERNIGHTENGINE_API const AstrometricSolution& astrometry() const noexcept;

    /// Where it came from and what has been done to it.
    ///
    /// @return the lineage.
    [[nodiscard]] AFTERNIGHTENGINE_API const Provenance& provenance() const noexcept;

    /// What the source file said, kept as it said it.
    ///
    /// @return the preserved cards and the source encoding.
    [[nodiscard]] AFTERNIGHTENGINE_API const CodecMetadata& codec() const noexcept;

    /// How the image is to be written.
    ///
    /// @return the intent, or an empty optional when nothing has decided. Absent is the default and
    ///         a codec asked to write without one is being asked to invent a decision.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::optional<OutputIntent>& outputIntent() const noexcept;

    /// Whether two aggregates say the same thing.
    ///
    /// @param left one aggregate.
    /// @param right the other.
    /// @return true when all six members are equal.
    [[nodiscard]] friend bool operator==(const ImageMetadata& left, const ImageMetadata& right) = default;

private:
    /// What the instrument was doing.
    AcquisitionMetadata m_acquisition{};

    /// What has been done to the samples and under which encoding.
    ImageSemantics m_semantics{};

    /// Where the image is pointing.
    AstrometricSolution m_astrometry{};

    /// Where it came from and what has been done to it.
    Provenance m_provenance{};

    /// What the source file said.
    CodecMetadata m_codec{};

    /// How the image is to be written, when something has decided.
    std::optional<OutputIntent> m_outputIntent{};
};

/// What an operation states it changed about an image's metadata.
///
/// A plain aggregate of optionals: a member left out is a member the operation did not touch. It is
/// the only way metadata changes, and the fields it does *not* have are as much of its contract as
/// the ones it does - there is no way to reach the acquisition record through it, and no way to
/// change the lineage except by adding to it.
///
/// A transition that states nothing and carries no record is a legitimate no-op, and applying it
/// returns the metadata unchanged. Anything else needs the record.
struct MetadataTransition
{
    /// The meaning the samples now have. Stated by an operation that changed what they mean - a
    /// transfer function applied or removed, a calibration step completed.
    std::optional<ImageSemantics> semantics{};

    /// The solution the image now has. Stated by an operation that solved, refined, or invalidated
    /// one; an absent solution is stated by giving a default-constructed `AstrometricSolution`
    /// here, which is how an operation says the old one no longer applies.
    std::optional<AstrometricSolution> astrometry{};

    /// The decision about how the image is to be written. Stated by whatever decides.
    std::optional<OutputIntent> outputIntent{};

    /// Whether the existing output intent is withdrawn. Stated by an operation whose result the
    /// previous decision no longer describes. Cannot be combined with stating a new one.
    bool clearOutputIntent = false;

    /// Whether the preserved source cards are dropped. Stated by an operation whose result is not
    /// the frame those cards describe - a stack of fifty inputs, a crop that invalidates a header.
    /// Cannot be combined with stating new cards.
    bool clearCodecMetadata = false;

    /// The cards the image carries from now on, in place of the preserved ones. Stated by an operation
    /// that writes cards of its own beside the source's - a product recording on its processing-state
    /// cards what the samples now are - which it reads from the image, changes and states whole, so a
    /// result keeps its own samples and needs no copy to carry new cards.
    std::optional<CodecMetadata> codec{};

    /// What did it. Required whenever any of the fields above is stated, and permitted on its own
    /// by an operation that changed nothing and wants the fact recorded.
    std::optional<ProvenanceEntry> record{};
};

/// Whether a transition changes what an image means.
///
/// The question an image asks to decide whether its semantic generation moves. Exactly the two
/// semantic objects count: a change to either makes a result computed before it wrong. The output
/// intent and the cards - preserved, dropped or stated - do not, because nothing computed from the
/// image depends on how it is going to be written or on what a card says.
///
/// Thread-safe and allocation-free.
///
/// @param transition the stated change.
/// @return true when the transition states a new meaning or a new solution.
[[nodiscard]] AFTERNIGHTENGINE_API bool changesMeaning(const MetadataTransition& transition) noexcept;

/// Whether a transition states any change at all.
///
/// A transition carrying only a record changes nothing but the lineage, and one carrying nothing at
/// all changes nothing.
///
/// Thread-safe and allocation-free.
///
/// @param transition the stated change.
/// @return true when any field other than the record is stated.
[[nodiscard]] AFTERNIGHTENGINE_API bool changesAnything(const MetadataTransition& transition) noexcept;

/// The metadata an image has after an operation states what it changed.
///
/// @param metadata what the image had.
/// @param transition what the operation changed.
/// @return the new aggregate; `ErrorCode::IncompatibleArguments` when the transition both states
///         and withdraws an output intent, or both states cards and drops them;
///         `ErrorCode::InvalidArgument` when a stated change carries no record; or
///         `ErrorCode::LimitExceeded` when the lineage already holds
///         `kMaxProvenanceEntries` entries and the record cannot be appended. The original is
///         never changed.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageMetadata> applyMetadataTransition(const ImageMetadata& metadata,
                                                                                 const MetadataTransition& transition);

} // namespace an
