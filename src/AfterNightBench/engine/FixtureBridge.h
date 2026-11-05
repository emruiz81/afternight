// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineBench - turning an engine-neutral fixture into something this engine can read.
//
// The benchmark system describes pixel data in the abstract, because the layer that describes it
// links no engine: its fixtures and oracles must not be computed by what they judge. That leaves a
// gap: the runner receives a descriptor and a block of bytes and has to turn them into whatever the
// engine calls an image.
//
// This is that translation for the engine, in one place rather than at each scenario. It
// exists as a file of its own for two reasons. The first is that the translation is not obvious -
// a bench descriptor states shape and representation and says nothing about what the samples mean,
// while an `an::ImageDescriptor` refuses to exist without that - so the decision has to be written
// down somewhere a reader can find it. The second is that most of the ways it can go wrong are
// silent: a stride read as tight when the fixture stated one, a big-endian payload read as native,
// a 64-bit integer frame quietly narrowed. Each of those produces a benchmark that measures
// something, reports a digest, and publishes it under the name of something else.
//
// It sits in the runner's own tree, which links AfterNightEngine and the engine-neutral common
// layer, and it names `an::bench` types and `an::` types in one file precisely because that is the
// seam - the common layer must not know about an engine, and the runner has to.

#pragma once

#include "Descriptors.h"
#include "Fixture.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace an::bench::engine
{

/// Why a benchmark fixture cannot be read by this engine as it stands.
///
/// A named reason rather than a message, because the runner's answer differs by reason: some of
/// these mean the scenario is not one this engine implements and belongs in a skip line,
/// and some mean the scenario file describes something impossible and belongs in a failure. A
/// message would leave the caller parsing prose to tell them apart.
enum class FixtureBridgeFailure : std::uint8_t
{
    /// No failure: the fixture can be read where it is.
    None,

    /// The descriptor states a shape no image has - a zero dimension, or byte counts that overflow.
    DescriptorNotUsable,

    /// The representation is one the benchmark system can describe and this engine does not store.
    /// The 64-bit integer FITS cases are the whole of this set, and they are refused rather than
    /// narrowed, because a narrowing is a measurement of a different frame.
    SampleTypeNotResident,

    /// The arrangement is one this bridge does not build a geometry for.
    ///
    /// The semi-planar case is the one that would reach it, and today it does not: the shared
    /// benchmark layer already refuses a semi-planar descriptor as one it cannot size, and that
    /// check runs first. This value is what an arrangement added to the benchmark vocabulary
    /// without a geometry here would report, rather than the bridge quietly placing it as planar.
    ArrangementNotSupported,

    /// The payload is not in this host's byte order. It can be converted - that is what
    /// `Image/ImageConversion.h` is for - but not read in place, and reading it in place is what
    /// this bridge does.
    ByteOrderNotNative,

    /// The mosaic the descriptor names is not one this engine describes.
    CfaPatternNotSupported,

    /// The engine's configured limits refuse the extent or the total size.
    ExtentRefused,

    /// The strides the descriptor states do not describe a buffer: shorter than a row, or not a
    /// whole number of samples.
    StrideRefused,

    /// The description and the meaning stated beside it cannot describe one image. The refusal is
    /// the descriptor's own, and it is reported here rather than resolved.
    MeaningRefused,

    /// The fixture holds fewer bytes than its own descriptor says it does.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    PayloadTooSmall
};

/// What a benchmark descriptor does not carry and an engine descriptor requires.
///
/// A bench descriptor is shape and representation: it exists so every run can be handed identical
/// bytes, and an engine's opinion of what those bytes mean does not belong in it. This engine
/// refuses to describe an image without that opinion, so the scenario states it here - and states
/// it explicitly rather than having the bridge infer it, because inferring a numeric domain from a
/// sample type is exactly what this engine refuses to do.
struct FixtureSemantics
{
    /// What the channels are.
    an::ChannelSchema channelSchema;

    /// What basis they are expressed in.
    an::ColorSpace colorSpace;

    /// What the magnitudes mean.
    an::NumericDomain numericDomain;

    /// Which corner row zero is at.
    an::CoordinateOrigin coordinateOrigin;

    /// The width of one pixel divided by its height.
    double pixelAspectRatio;
};

/// A fixture's shape and meaning as this engine states them.
///
/// The two halves a caller needs and neither of which can be derived from the other: where the
/// samples sit, and what they are. `an::layoutDescribes` holds for the pair by construction.
struct BridgedFixture
{
    /// Where the samples sit, with the fixture's own strides kept exactly as stated.
    an::ImageLayout layout;

    /// What the samples are, and what they mean.
    an::ImageDescriptor descriptor;

    /// How the planes are spread, which the layout implies and a reader should not have to derive.
    an::StorageArrangement arrangement;
};

/// The meaning to state for a fixture whose scenario says nothing more.
///
/// One channel is monochrome and three are RGB; every other count is `Named`, because a benchmark
/// fixture with five channels is a stack of filters rather than a colour image. The basis is
/// unspecified and the domain is a fraction of full scale, which is what a synthetic ramp is, and
/// the origin is the top-left corner that every codec in this domain writes from.
///
/// A scenario that means something else states it rather than adjusting this.
///
/// @param descriptor the benchmark descriptor to derive from; only its channel count is read.
/// @return the meaning, which `bridgeFixture` will still check against the representation.
[[nodiscard]] FixtureSemantics defaultFixtureSemantics(const SampleDescriptor& descriptor) noexcept;

/// The engine representation a benchmark sample type is.
///
/// A closed table with two gaps. The benchmark vocabulary carries the 64-bit integer cases because
/// a FITS file defines them and a scenario has to be able to describe one; this engine stores eight
/// representations and neither of those is among them.
///
/// @param type the benchmark representation.
/// @return the engine representation, or an empty optional for one it does not store.
[[nodiscard]] std::optional<an::SampleType> toEngineSampleType(SampleType type) noexcept;

/// The benchmark representation an engine sample type is.
///
/// The other direction of the same table, total because every engine representation is one the
/// benchmark vocabulary can describe.
///
/// @param type the engine representation.
/// @return the benchmark representation.
[[nodiscard]] SampleType toBenchSampleType(an::SampleType type) noexcept;

/// The engine arrangement a benchmark layout is.
///
/// @param layout the benchmark layout.
/// @return the arrangement, or an empty optional for a layout this bridge builds no geometry for -
///         semi-planar, whose split a channel count does not state, and packed CFA, which is
///         described as a mosaiced single plane through the descriptor rather than as an
///         arrangement.
[[nodiscard]] std::optional<an::StorageArrangement> toEngineArrangement(Layout layout) noexcept;

/// The engine byte order a benchmark byte order is, resolved against this host.
///
/// `ByteOrder::Native` is resolved rather than passed through, because the whole point of the
/// benchmark vocabulary carrying an explicit order is that a fixture digest means the same thing on
/// either host.
///
/// @param order the benchmark order.
/// @return the engine order, which is never `an::ByteOrder::Native`.
[[nodiscard]] an::ByteOrder toEngineByteOrder(ByteOrder order) noexcept;

/// The engine mosaic a benchmark CFA string names.
///
/// @param pattern the four-letter pattern, or an empty string for an image with no mosaic.
/// @return the pattern, or an empty optional for a string this engine does not describe. Comparison
///         is case-sensitive and the four Bayer arrangements are the whole of the set.
[[nodiscard]] std::optional<an::CfaPattern> toEngineCfaPattern(const std::string& pattern) noexcept;

/// Whether a fixture can be read by this engine as it stands.
///
/// The order is a contract: the descriptor's own usability first, then the representation, then the
/// arrangement and the order, then the geometry, then the meaning, then the bytes. So a fixture
/// with two problems reports the same one on every run, and a reason is never derived from a value
/// that had already been refused.
///
/// @param descriptor what the fixture is.
/// @param semantics what its samples mean.
/// @param limits the engine limits the geometry is checked against.
/// @param payloadBytes how many bytes the fixture actually holds.
/// @return `FixtureBridgeFailure::None` when the fixture can be read; otherwise the first rule that
///         refused.
[[nodiscard]] FixtureBridgeFailure checkFixture(const SampleDescriptor& descriptor, const FixtureSemantics& semantics,
                                                const an::EngineLimits& limits, std::uint64_t payloadBytes) noexcept;

/// A fixture's shape and meaning as this engine states them.
///
/// The strides are kept exactly as the descriptor states them, including a stride wider than a row:
/// a scenario that declares a noncanonical layout is measuring that layout, and a bridge that
/// tightened it would measure something else and report it under the scenario's name.
///
/// @param descriptor what the fixture is.
/// @param semantics what its samples mean.
/// @param limits the engine limits the geometry is checked against.
/// @param payloadBytes how many bytes the fixture actually holds.
/// @return the pair, or an empty optional exactly when `checkFixture` would name a reason.
[[nodiscard]] std::optional<BridgedFixture> bridgeFixture(const SampleDescriptor& descriptor,
                                                          const FixtureSemantics& semantics,
                                                          const an::EngineLimits& limits,
                                                          std::uint64_t payloadBytes) noexcept;

/// The failure's own name, which is what a skip line reports.
///
/// @param failure the reason to name. A value outside the enumeration is named
///        `"UnknownFixtureBridgeFailure"`.
/// @return a null-terminated string literal, which outlives every caller and is never freed.
[[nodiscard]] const char* fixtureBridgeFailureName(FixtureBridgeFailure failure) noexcept;

/// A sentence a runner can put in a skip reason.
///
/// @param failure why the fixture was refused.
/// @param descriptor what the fixture is, which the sentence names so a reader does not have to
///        find the scenario to know which shape was refused.
/// @return the sentence, freshly built.
[[nodiscard]] std::string describeFixtureBridgeFailure(FixtureBridgeFailure failure,
                                                       const SampleDescriptor& descriptor);

/// A read-only view over a fixture's own bytes.
///
/// The view borrows: it points into the fixture's buffer and owns nothing, so it must not outlive
/// the fixture, and a fixture that is reassigned while a view of it exists leaves that view
/// dangling. That is the same rule every view in this engine follows, and it is stated here because
/// a benchmark's fixture is often a member that outlives one scenario and is replaced before the
/// next.
///
/// @tparam SampleT the sample type the layout's element size must match.
/// @param fixture the fixture whose bytes are to be viewed.
/// @param layout where the samples sit, as `bridgeFixture` reported.
/// @return the view, or an empty optional when the sample type, the layout and the buffer do not
///         describe one image.
template <an::ViewableSample SampleT>
[[nodiscard]] std::optional<an::ImageView<const SampleT>> viewFixture(const Fixture& fixture,
                                                                      const an::ImageLayout& layout) noexcept
{
    if (layout.elementSizeBytes() != sizeof(SampleT) || fixture.bytes.size() < layout.totalBytes())
    {
        return std::nullopt;
    }
    const std::span<const SampleT> samples{reinterpret_cast<const SampleT*>(fixture.bytes.data()),
                                           static_cast<std::size_t>(layout.totalBytes() / sizeof(SampleT))};
    return an::ImageView<const SampleT>::create(samples, layout);
}

/// The benchmark backend identity for what an operation actually dispatched to.
///
/// The benchmark vocabulary names implementation *kinds* rather than instruction sets, so the
/// mapping is by backend: this engine's scalar reference is `Scalar`, and its compiled CPU kernels
/// are `CompilerVectorized`, because they are ordinary C++ compiled for whatever instruction set
/// the dispatch table selected rather than hand-written vector code. `Avx2` and `Avx512` are
/// therefore not returned by this build; an explicit-SIMD kernel, when one is written, is what
/// earns them. A run that could not resolve a backend at all reports `Unspecified` rather than
/// guessing, because a benchmark that names the wrong implementation is worse than one that names
/// none.
///
/// @param selection what the dispatch table reported, or a failure.
/// @return the benchmark identity.
[[nodiscard]] BackendId toBenchBackend(const an::Result<an::BackendSelection>& selection) noexcept;

/// A sentence a runner can put in a skip reason when the engine refused an operation.
///
/// The code's own name is included beside the message, because the message is written for a person
/// and is free to change between builds while the name is not - and a skip line is read by both.
/// A numeric library/platform error is retained from the failure or its first cause carrying one.
///
/// @param what the operation being described, in the runner's own words.
/// @param status the failure the engine reported.
/// @return the sentence, freshly built.
[[nodiscard]] std::string describeEngineFailure(const std::string& what, const an::Status& status);

/// The failure `describeEngineFailure` last described on the calling thread.
struct DescribedEngineFailure
{
    /// The failure's own code.
    an::ErrorCode code = an::ErrorCode::Internal;

    /// Whether the failure or any cause beneath it is `ErrorCode::Cancelled`: an operation that
    /// stopped because it was asked to, whatever it named the failure it returned.
    bool cancelled = false;
};

/// Takes the failure `describeEngineFailure` last described on the calling thread, and forgets it.
///
/// A measured body keeps only the sentence it built, and the cancellation probe needs the code
/// rather than the words: whether an operation stopped because it was asked to is a fact about the
/// operation, and a message is written for a person and never parsed for one.
///
/// @return the failure, or nothing when none has been described on this thread since the last take.
[[nodiscard]] std::optional<DescribedEngineFailure> takeLastDescribedEngineFailure() noexcept;

} // namespace an::bench::engine
