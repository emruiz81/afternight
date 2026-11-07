// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What one run of a process is, what its body reads and writes, and the engine work the process framework does around
/// the body: the destination that starts as a copy of the source, the channel or colour component it runs on, the
/// modulation and the mask that blend its result back toward the source, and the preview area's crop and composite.
///
/// **A body reads one image and writes one.** The framework takes the lease, so the body never waits for one: an Apply
/// to the view's image holds the document's writer for the whole edit, a preview reads the target under a read lease,
/// a preview area is a crop and a sequence frame a frame the framework read. The body is handed that image as a
/// `ProcessSource` - borrowed for the run - and a `ProcessDestination`, an asset the run owns that starts as a copy of
/// the source, so a body that writes nothing publishes the source unchanged and a body that fails or is stopped
/// publishes nothing. The destination is written in place through its write lease, or replaced by another image when
/// the body's result has another shape or representation.
///
/// **A body that writes every sample is not handed a copy.** A process declares, for the parameters a run holds, what
/// its body writes (`ProcessBodyCoverage`). A body that writes every sample of every channel, or replaces the
/// destination, and reads none of it first is handed a destination whose samples start unwritten whenever the source is
/// one a pointwise program reads where it lies - planar, every sample valid - and the channel scope and the shared
/// helpers read the source rather than the destination. Copying a 72-megapixel frame into a destination the body then
/// overwrites cost a preview about as much as the body did. A destination always states whether it holds the source
/// (`ProcessDestination::holdsSource`), and a helper that needs the source there asks for it
/// (`ProcessDestination::holdSource`).
///
/// **Every choice a worker reads was snapshotted on the GUI thread.** The channel the dialog's selector named, the
/// modulation, the mask and the preview area travel with the run as values; nothing here reads a widget.
///
/// **What a run converts of what it reads, a preview's next frame reads again.** A component of another encoding
/// starts as the source converted into that encoding, and the blend reads the source and the mask laid out a plane per
/// channel in the result's representation. For a real-time preview each is the same conversion of the same image
/// frame after frame: a frame's sources carry `ProcessConversions`, which hands the channel scope and the blend what
/// earlier frames made of an image's revision and keeps what this frame makes, and the dialog keeps those between
/// frames (`ProcessConversionCache`). A conversion is filed under the revision of the asset it was made from, which
/// moves whenever the image changes, inside the dialog or outside it, so none is ever read for samples it was not made
/// from. An Apply converts in the call.
///
/// **A body may hand over results it made beside its own.** A process whose one computation makes several images a
/// parameter only chooses among - the corrected frame, the surface it removed, the structure it kept out of the fit -
/// writes the one the run asks for into the destination and hands the others to the run, each under the key a run for
/// it would carry (`ProcessDestination::keepAlternative`). A preview blends each as it blends its result and keeps
/// them, so a change to one of those parameters shows the image at once, with no new run.
///
/// Everything below runs on a worker under an admitted execution, except the channel choices table, which is the
/// selector's, and the conversion cache, which is its dialog's.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QRect>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::ui
{

/// Why a process runs.
enum class ProcessRunKind : std::uint8_t
{
    /// A real-time preview of the dialog's target, shown in the dialog's own preview view.
    Preview,

    /// An Apply to the target image itself, recorded in its history.
    Apply,

    /// An Apply whose result becomes a new image in a view of its own.
    NewImage,

    /// The preview's result, taken as a new image.
    Snapshot,

    /// One frame of an Apply over a sequence.
    SequenceFrame
};

/// A run kind's name, for messages and records.
///
/// @param kind the kind.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONTROLS_API const char* processRunKindName(ProcessRunKind kind) noexcept;

/// What a process body writes of its destination, as the process declares it for a run's parameters.
enum class ProcessBodyCoverage : std::uint8_t
{
    /// It may leave a sample alone, which is then the source's: the destination starts as a copy of the source. What a
    /// process that declares nothing is, and what a body that returns early for an identity setting needs.
    WritesPart,

    /// It writes every sample of every channel of its destination, or replaces the destination, and reads none of it
    /// before writing it. The destination may start unwritten.
    WritesEverySample
};

/// Which kind of frame a preview is: one shown while a person drags a control, or one shown once the parameters have
/// settled.
enum class PreviewFrameKind : std::uint8_t
{
    /// The parameters have settled: no control is held, or the one that was was let go or held still. What every run
    /// that is not a preview is, and every preview a change asks for without a held control.
    Settled,

    /// A control is being dragged. The frame runs on the session's preview reservation rather than every thread it
    /// could borrow, and a settled frame follows the drag.
    Interactive
};

/// A frame kind's name, for messages and records.
///
/// @param kind the kind.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONTROLS_API const char* previewFrameKindName(PreviewFrameKind kind) noexcept;

/// What an interactive frame of a process may run over, as the process declares it.
enum class PreviewScaling : std::uint8_t
{
    /// Every frame runs the body over the real image at full resolution: what a process that declares nothing is, and
    /// what a body must be whose result depends on where a pixel lies, on the image's size or on another image.
    FullOnly,

    /// The body maps every pixel by a rule that depends neither on where the pixel lies nor on how large the image is,
    /// and reads no other image, so an interactive frame may run it over what the preview view paints of the target:
    /// the target reduced by the view's factor, or the part the view shows of it at full scale. A body that measures
    /// its source takes the target's measurement on such a run, as it does on a preview area's
    /// (`ProcessRun::readsViewOfTarget`). A body that reads a pixel's neighbours may declare this too when it scales
    /// its reach by the reduction (`ProcessRun::proxyFactor`) - a blur's sigma - and states how far it reads around a
    /// region (`ImageRTPreviewControl::previewRegionMargin`).
    Decimated,

    /// The body's answer at a pixel depends on the pixel grid - a kernel's radius, a patch's or an element's size - by
    /// no rule that scales to a reduction, and on nothing farther than a reach the process states around the pixel
    /// (`ImageRTPreviewControl::previewRegionMargin`). An interactive frame runs it at full scale over the part of the
    /// target the preview view shows, grown by that reach, and shows that part alone: there it is the whole result's
    /// pixels, whatever the view's zoom. Where the view shows the whole target, or a frame cannot be shown by a part,
    /// every frame runs over the whole target and is a settled one. A body that measures its source takes the target's
    /// measurement on such a run, as it does on a preview area's (`ProcessRun::readsViewOfTarget`).
    NativeRegion
};

/// A preview scaling's name, for messages and records.
///
/// @param scaling the scaling.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONTROLS_API const char* previewScalingName(PreviewScaling scaling) noexcept;

/// What a run states about itself, decided on the GUI thread before its worker starts.
struct ProcessRun
{
    /// Why it runs.
    ProcessRunKind kind = ProcessRunKind::Apply;

    /// Which kind of frame a preview is; `PreviewFrameKind::Settled` for every run that is not a preview.
    PreviewFrameKind frameKind = PreviewFrameKind::Settled;

    /// The channel or colour component the dialog's selector named; `ChannelScope::Whole` for a process that offers no
    /// choice and for a grey image.
    an::ChannelScope channel = an::ChannelScope::Whole;

    /// The zero-based index of the frame a sequence run processes; zero otherwise.
    std::uint64_t frame = 0u;

    /// How many frames a sequence run processes; zero otherwise.
    std::uint64_t frameCount = 0u;

    /// What the body writes of its destination, as the process declared it for the run's parameters; the framework
    /// hands it a destination accordingly (`ProcessDestination::forBody`).
    ProcessBodyCoverage coverage = ProcessBodyCoverage::WritesPart;

    /// How many of the target's pixels each way one pixel of the source stands for: one for a run over the target, a
    /// part of it or a frame of a sequence; more for an interactive frame over the target reduced by the preview
    /// view's factor, or for the sample of the target the frame's histogram is counted on.
    std::uint32_t proxyFactor = 1u;

    /// Whether the source is a part of the target the framework cut for the run - the preview area, or the region of
    /// the target an interactive frame runs over - rather than the whole of it.
    bool partOfTarget = false;

    /// Where in the target, in its pixels, the part the source is lies: the preview area, or the part an interactive
    /// frame over what the preview view shows runs over, its reach included. A body that reads another image of the
    /// target's size reads the same part of it. Empty for a source that is no rectangle of the target - the whole
    /// target, a reduction or a sample of it, a row of its codes.
    QRect part;

    /// Whether the result is shown rather than applied: a preview or a snapshot of one.
    [[nodiscard]] constexpr bool isForPreview() const noexcept
    {
        return kind == ProcessRunKind::Preview || kind == ProcessRunKind::Snapshot;
    }

    /// Whether the source is a reduction or a part of the target rather than the target itself. Its revision is its
    /// own and never the target's, and a measurement of it is not the target's: a body that measures its source takes
    /// the target's measurement instead.
    [[nodiscard]] constexpr bool readsViewOfTarget() const noexcept
    {
        return proxyFactor > 1u || partOfTarget;
    }

    /// Whether the run is one frame of a sequence.
    [[nodiscard]] constexpr bool isForSequence() const noexcept
    {
        return kind == ProcessRunKind::SequenceFrame;
    }
};

/// The history step an Apply records, decided from the parameters on the GUI thread.
struct ProcessStep
{
    /// The step's name: the process's, unless it says otherwise.
    std::string name;

    /// What the step did, for the history window.
    std::string description;
};

/// One conversion of an image a run read, kept under the revision of the asset it was made from: the forward half of
/// a channel scope over it, or the image laid out a plane per channel in a representation. Immutable once made, and
/// read on any thread.
struct ProcessConversion
{
    /// The revision of the asset the conversion was made from; it is read only for a source of the same revision.
    an::consumer::AssetRevision source{};

    /// The source converted into a scope's encoding for the request it states, or null.
    std::shared_ptr<const an::ChannelScopePreparation> prepared;

    /// The image laid out a plane per channel in the representation it holds, or null.
    std::shared_ptr<const an::consumer::AnyImage> planar;

    /// How many bytes it holds.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t byteCount() const noexcept;
};

/// The conversions one run reads and makes of the images it reads.
///
/// The framework's channel scope and blend use it on the run's worker, through the sources that carry it
/// (`ProcessSource::reusing`): each takes what an earlier run made of the same revision, and keeps what it makes for
/// the run's later reads and for the runs that follow. The dialog hands a run what it keeps when the run is issued and
/// keeps what the run made when it lands (`ProcessConversionCache`).
class ProcessConversions
{
public:
    /// The conversions of one run.
    ///
    /// @param engine the engine context of the run's execution, which a conversion this run makes takes its memory
    ///        from; it must outlive what the run keeps.
    /// @param kept what earlier runs made, which this run may read.
    AFTERNIGHTCONTROLS_API ProcessConversions(const an::EngineContext& engine, std::vector<ProcessConversion> kept);

    ProcessConversions(const ProcessConversions&) = delete;
    ProcessConversions& operator=(const ProcessConversions&) = delete;

    /// The engine context a conversion this run makes takes its memory from.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const an::EngineContext& engine() const noexcept;

    /// The forward half of a scope kept for a revision and a request, counted as reused when one is; or null.
    ///
    /// @param source the revision of the asset the run reads.
    /// @param request the scope's request.
    /// @return the preparation, or null.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::shared_ptr<const an::ChannelScopePreparation>
    prepared(const an::consumer::AssetRevision& source, const an::ChannelScopeRequest& request);

    /// The image laid out a plane per channel in a representation, kept for a revision, counted as reused when one is;
    /// or null.
    ///
    /// @param source the revision of the asset the run reads.
    /// @param sampleType the representation.
    /// @return the image, or null.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::shared_ptr<const an::consumer::AnyImage>
    planar(const an::consumer::AssetRevision& source, an::SampleType sampleType);

    /// Keeps a conversion this run made, for its later reads and for the runs that follow.
    ///
    /// @param conversion the conversion; one that holds nothing, or is of no asset, changes nothing.
    AFTERNIGHTCONTROLS_API void keep(ProcessConversion conversion);

    /// What this run made, in the order it made them.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<ProcessConversion> made() const;

    /// What this run read of what earlier runs kept, each once.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<ProcessConversion> reused() const;

    /// How many forward halves of a scope this run made, and how many times it read one kept.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t scopesPrepared() const;
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t scopesReused() const;

    /// How many images laid out a plane per channel this run made, and how many times it read one kept.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t planarMade() const;
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t planarReused() const;

private:
    /// Notes a kept conversion as read, under the lock.
    void noteReused(const ProcessConversion& conversion);

    const an::EngineContext* m_engine;
    mutable std::mutex m_lock;
    std::vector<ProcessConversion> m_kept;
    std::vector<ProcessConversion> m_made;
    std::vector<ProcessConversion> m_reused;
    std::uint64_t m_scopesReused = 0u;
    std::uint64_t m_planarReused = 0u;
};

/// The conversions a dialog's runs made, kept for the runs that follow: at most a capacity of them, the one least
/// recently kept or read let go first; one forward half of a scope per revision of an image, the latest; and none of a
/// revision of an asset older than the newest kept of it, which no run can read again. GUI thread only; what it hands
/// out is immutable and may be read on any thread.
class ProcessConversionCache
{
public:
    /// A cache keeping at most `capacity` conversions.
    ///
    /// @param capacity the most conversions kept; zero keeps none.
    AFTERNIGHTCONTROLS_API explicit ProcessConversionCache(std::size_t capacity);

    /// What is kept, for a run to read.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<ProcessConversion> kept() const;

    /// Keeps what a run made, as the most recently kept, after what it read of what was kept. One already kept for the
    /// same revision and the same request or representation is replaced.
    ///
    /// @param made what the run made, in order.
    /// @param reused what it read of what was kept, which is made as recently used as what it made.
    AFTERNIGHTCONTROLS_API void keep(const std::vector<ProcessConversion>& made,
                                     const std::vector<ProcessConversion>& reused = {});

    /// How many conversions are kept, and how many bytes they hold.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::size_t size() const noexcept;
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t byteCount() const noexcept;

    /// Lets every conversion go.
    AFTERNIGHTCONTROLS_API void clear() noexcept;

private:
    std::size_t m_capacity;
    std::vector<ProcessConversion> m_kept;
};

/// What a process body reads: one image, borrowed for the run under a lease the framework holds.
///
/// A view, not an owner: it is valid while the run is, and a body never keeps it.
class AFTERNIGHTCONTROLS_API ProcessSource
{
public:
    /// A source over an image the caller holds a lease on.
    ///
    /// @param image the image.
    /// @param validity its packed mask, or null when its rule needs none.
    /// @param revision the revision of the asset it was read from.
    ProcessSource(const an::consumer::AnyImage& image, const an::ValidityMask* validity,
                  an::consumer::AssetRevision revision) noexcept;

    /// The source a read lease reads.
    ///
    /// @param lease the lease; it must outlive the source.
    /// @return the source.
    [[nodiscard]] static ProcessSource of(const an::consumer::AssetReadLease& lease) noexcept;

    /// The source a write lease holds, for an edit that reads the image it is about to replace.
    ///
    /// @param lease the lease; it must outlive the source.
    /// @return the source.
    [[nodiscard]] static ProcessSource of(const an::consumer::AssetWriteLease& lease) noexcept;

    /// The image.
    [[nodiscard]] const an::consumer::AnyImage& image() const noexcept;

    /// Its packed validity mask, or null when its rule needs none.
    [[nodiscard]] const an::ValidityMask* validity() const noexcept;

    /// The revision of the asset it was read from.
    [[nodiscard]] an::consumer::AssetRevision revision() const noexcept;

    /// What the image is; nothing for an image that holds nothing.
    [[nodiscard]] std::optional<an::ImageDescriptor> descriptor() const;

    /// What is known about the image.
    [[nodiscard]] const an::ImageMetadata& metadata() const noexcept;

    /// The same source, whose conversions the framework's channel scope and blend take from a run's conversions and
    /// keep there.
    ///
    /// @param conversions the run's conversions, which must outlive the source; null for none.
    /// @return the source.
    [[nodiscard]] ProcessSource reusing(ProcessConversions* conversions) const noexcept;

    /// The run's conversions the source reads and keeps its own in, or null for a run that keeps none.
    [[nodiscard]] ProcessConversions* conversions() const noexcept;

private:
    const an::consumer::AnyImage* m_image;
    const an::ValidityMask* m_validity;
    an::consumer::AssetRevision m_revision;
    ProcessConversions* m_conversions = nullptr;
};

/// A result a body made beside the one it writes into its destination: the image a run whose parameters carry `key`
/// would have made, from the same computation.
struct ProcessAlternative
{
    /// What a preview finds the image by: the key the process names for the parameters that would have made it
    /// (`ImageRTPreviewControl::previewResultKey`).
    std::string key;

    /// The image, an asset of the run's session that nothing else leases.
    an::consumer::ImageAsset image;
};

/// Where a process body writes: an asset the run owns, held under its write lease.
///
/// It has the source's description, metadata and validity, laid out a plane per channel. It starts as a copy of the
/// source's samples, so whatever the body does not write is the source's - or, for a body that writes every sample,
/// with its samples unwritten, which `holdsSource` says. Move-only.
class AFTERNIGHTCONTROLS_API ProcessDestination
{
public:
    /// A destination of nothing.
    ProcessDestination() noexcept;

    /// Lets the write lease and the asset go.
    ~ProcessDestination();

    /// Takes over another destination.
    ///
    /// @param other the destination to take over.
    ProcessDestination(ProcessDestination&& other) noexcept;

    /// Lets this destination go and takes over another.
    ///
    /// @param other the destination to take over.
    /// @return this destination.
    ProcessDestination& operator=(ProcessDestination&& other) noexcept;

    ProcessDestination(const ProcessDestination&) = delete;
    ProcessDestination& operator=(const ProcessDestination&) = delete;

    /// A destination that starts as a copy of the source, planar whatever the source's arrangement.
    ///
    /// @param session the session the destination's asset belongs to.
    /// @param engine the engine context of the run's execution.
    /// @param execution the run's admitted execution.
    /// @param source what the copy is of.
    /// @return the destination; or what the allocation, the copy or the adoption refused.
    [[nodiscard]] static an::Result<ProcessDestination> copyOf(const an::consumer::EngineSession& session,
                                                               const an::EngineContext& engine,
                                                               const an::ExecutionContext& execution,
                                                               const ProcessSource& source);

    /// A destination with the source's description, metadata and validity, planar, whose samples start unwritten:
    /// for a body that writes every one of them.
    ///
    /// @param session the session the destination's asset belongs to.
    /// @param engine the engine context of the run's execution.
    /// @param source what the destination is shaped as.
    /// @return the destination; or what the allocation or the adoption refused.
    [[nodiscard]] static an::Result<ProcessDestination> unwrittenFor(const an::consumer::EngineSession& session,
                                                                     const an::EngineContext& engine,
                                                                     const ProcessSource& source);

    /// The destination a body of a coverage is handed: unwritten when the body writes every sample and the source is
    /// one every helper reads where it lies - planar, under a rule that calls every sample valid, with no mask - and a
    /// copy of the source otherwise.
    ///
    /// @param session the session the destination's asset belongs to.
    /// @param engine the engine context of the run's execution.
    /// @param execution the run's admitted execution.
    /// @param source what the run reads.
    /// @param coverage what the body writes, as the process declared it for the run's parameters.
    /// @return the destination; or what the allocation, the copy or the adoption refused.
    [[nodiscard]] static an::Result<ProcessDestination>
    forBody(const an::consumer::EngineSession& session, const an::EngineContext& engine,
            const an::ExecutionContext& execution, const ProcessSource& source, ProcessBodyCoverage coverage);

    /// Whether this holds an asset.
    [[nodiscard]] bool valid() const noexcept;

    /// Whether every sample the body has not written is the source's: true for a destination made as a copy of the
    /// source or brought to hold it, false for one that started unwritten and for a replacement.
    [[nodiscard]] bool holdsSource() const noexcept;

    /// Writes the source's samples into a destination that does not hold them, for a helper that works in place on
    /// the destination; nothing for one that holds them already.
    ///
    /// @param execution the run's admitted execution.
    /// @param source what the run reads; the destination must still be described as it.
    /// @return success; `ErrorCode::NotInitialized` for a destination of nothing; `ErrorCode::IncompatibleArguments`
    ///         for a destination described otherwise than the source; or what the copy refused.
    [[nodiscard]] an::Status holdSource(const an::ExecutionContext& execution, const ProcessSource& source);

    /// The write lease on the asset the destination holds now. Calling it on a destination of nothing is a defect.
    [[nodiscard]] an::consumer::AssetWriteLease& asset() noexcept;

    /// The write lease on the asset the destination holds now, for reading. Calling it on a destination of nothing is a
    /// defect.
    [[nodiscard]] const an::consumer::AssetWriteLease& asset() const noexcept;

    /// Makes another image the destination: the body's result has another shape or representation than the source.
    ///
    /// @param replacement the new image, of the run's session; nothing else may lease it.
    /// @return success; `ErrorCode::NotInitialized` for a replacement that holds nothing or a destination of nothing;
    ///         `ErrorCode::Busy` when the replacement is leased. On a failure the destination is unchanged.
    [[nodiscard]] an::Status replace(an::consumer::ImageAsset&& replacement);

    /// Lets the write lease go, publishing the destination's revision, and hands the asset over.
    ///
    /// @return the asset, or `ErrorCode::NotInitialized` for a destination of nothing.
    [[nodiscard]] an::Result<an::consumer::ImageAsset> release();

    /// Hands the run an image the body made beside its result: what a run whose parameters carry `key` would have
    /// written here. A preview blends it by the modulation and the mask as it blends the result and keeps it, and
    /// shows it when the parameters come to carry that key; every other run lets it go. One kept under the same key
    /// before is replaced.
    ///
    /// @param key the key, not empty.
    /// @param image the image, an asset of the run's session that nothing else leases, of the source's width and
    /// height.
    /// @return success; `ErrorCode::InvalidArgument` for an empty key; `ErrorCode::NotInitialized` for an image that
    ///         holds nothing. On a failure nothing is kept.
    [[nodiscard]] an::Status keepAlternative(std::string key, an::consumer::ImageAsset&& image);

    /// Takes the images the body handed over, in the order it handed them, leaving none.
    [[nodiscard]] std::vector<ProcessAlternative> takeAlternatives() noexcept;

private:
    ProcessDestination(an::consumer::ImageAsset&& asset, an::consumer::AssetWriteLease&& lease,
                       bool holdsSource) noexcept;

    an::consumer::ImageAsset m_asset;
    an::consumer::AssetWriteLease m_lease;
    bool m_holdsSource = false;
    std::vector<ProcessAlternative> m_alternatives;
};

/// One entry of a process dialog's channel selector for a colour image.
struct ProcessChannelChoice
{
    /// What the entry runs the process on.
    an::ChannelScope scope = an::ChannelScope::Whole;

    /// The text the selector shows. Controls' own text: nothing reads it back to decide a scope.
    const char* label = "";

    /// The icon the selector shows beside it, by the name the icon provider knows it by.
    const char* icon = "";

    /// Whether a separator comes before the entry, opening a group.
    bool separatorBefore = false;
};

/// The one entry a grey image's selector shows, which runs the process on the whole image.
inline constexpr const char* kProcessGreyscaleChannelLabel = "Grayscale";

/// A colour image's channel selector, in the order it is shown: the whole image and its stored channels, then CIE
/// L*a*b* and L*C*h, CIE XYZ, HSL, HSV and YCbCr. The one table of what a process can be run on and how it is
/// labelled; the selector carries each entry's scope as its item data.
///
/// @return the twenty-one entries, static.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::span<const ProcessChannelChoice> processChannelChoices() noexcept;

/// The scope a selector label names, for a selector built before its items carried their scope.
///
/// @param label the label as the table spells it, or `kProcessGreyscaleChannelLabel`.
/// @return the scope, or nothing for a label the table does not have.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::ChannelScope> processChannelScopeForLabel(const QString& label);

/// The label of a scope a colour image's selector offers.
///
/// @param scope the scope.
/// @return the label, or an empty string for a scope the selector does not offer - the three HSI components.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString processChannelLabel(an::ChannelScope scope);

/// The scope a run takes for a choice over an image: the choice itself over a colour image, and the whole image over
/// an image with no red, green and blue to choose from.
///
/// @param choice what the selector named.
/// @param source what the image is.
/// @return the scope the run takes.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::ChannelScope
effectiveProcessChannel(an::ChannelScope choice, const an::ImageDescriptor& source) noexcept;

/// The description a channel or colour component of an image is taken under: the image's own, except that samples
/// stating no full scale - sensor codes, or a floating physical quantity, as every integer and every floating file
/// is read - are read as fractions of full scale, as the view shows them.
/// The samples are the same memory; every colour component, lightness and luma is taken under this description.
///
/// @param described what the image states it is.
/// @return the description to take a component under, or nothing when the image has no such description.
[[nodiscard]] inline std::optional<an::ImageDescriptor> componentReadingOf(const an::ImageDescriptor& described)
{
    const an::NumericDomain domain = described.numericDomain();
    if (domain != an::NumericDomain::PhysicalScaled && domain != an::NumericDomain::RawSensorCode)
    {
        return described;
    }
    an::ImageDescriptorRequest restated = described.toRequest();
    restated.numericDomain = an::NumericDomain::NormalizedLinear;
    return an::ImageDescriptor::create(restated);
}

/// The channel scope request a process runs a choice through: the source's red, green and blue read as linear for a
/// source that says it is linear and sRGB-encoded otherwise, with each component handed over in the unit interval.
///
/// @param scope the scope.
/// @param source what the image is.
/// @return the request.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::ChannelScopeRequest
processChannelScopeRequest(an::ChannelScope scope, const an::ImageDescriptor& source) noexcept;

/// Runs an operation over one scope of the source and recomposes the destination with what it wrote: the engine's
/// channel scope, under the run's execution. A grey source is run whole, whatever the scope.
///
/// A component of another encoding of a source that carries a run's conversions is converted once per revision of the
/// source: the forward half an earlier run kept is read, or the one made now is kept; one the budget refuses is
/// converted in the call. The answer is the same either way, sample for sample.
///
/// The destination must still have the source's description - a body calls this before it replaces the destination.
/// The destination's metadata is the source's, which the destination started with, so a run on a component of another
/// encoding keeps the source's solution, cards and acquisition record; what the body changes is the body's to state.
/// The scope is told whether the destination holds the source, so nothing of the source is copied into it twice; over
/// a destination that started unwritten, an operation that does not declare it writes every sample finds the source
/// in the whole image's output, copied by the scope. The body calls it before it writes the destination itself: a
/// destination that holds the source is taken to hold it still, in every sample the operation leaves alone.
///
/// @param execution the run's admitted execution, which the operation is given.
/// @param scope the scope the run snapshotted.
/// @param source what the run reads.
/// @param destination what the run writes; whatever the operation leaves alone is the source's.
/// @param operation what to run over the scoped samples.
/// @return what the scope did; `ErrorCode::IncompatibleArguments` for a destination no longer described as the source;
///         or what the scope or the operation refused. On a failure the destination may be partly written.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::ChannelScopeResult>
applyProcessChannelScope(const an::ExecutionContext& execution, an::ChannelScope scope, const ProcessSource& source,
                         ProcessDestination& destination, an::ChannelScopeOperation& operation);

/// How a run's result is blended back toward its source.
struct ProcessBlendRequest
{
    /// The weight of the result: one keeps the result, zero the source. Between them, every sample is the source's
    /// moved toward the result's by this much.
    double modulation = 1.0;

    /// A mask whose samples, as fractions of full scale, weigh the result pixel by pixel, or null for none. Borrowed
    /// for the call. It has the source's width and height and one channel, which weighs every channel, or the result's
    /// channel count.
    const an::consumer::AnyImage* mask = nullptr;

    /// The revision of the asset the mask was read from, under which its conversion is kept in the source's
    /// conversions; read only when the source carries them.
    an::consumer::AssetRevision maskRevision{};

    /// Whether the mask weighs the source rather than the result.
    bool invertMask = false;
};

/// Blends a run's result back toward its source by the modulation and the mask, in place in the destination, through
/// one pointwise program per channel in the result's representation.
///
/// A grey source under a colour result blends every channel toward the source's one. A source or mask of another
/// representation than the result, or not laid out a plane per channel, is first converted into it at full scale -
/// once per revision when the source carries a run's conversions, which keep it. Nothing is done when the modulation
/// is one and there is no mask.
///
/// @param engine the engine context of the run's execution, for the working copies a conversion needs.
/// @param execution the run's admitted execution.
/// @param source what the run read.
/// @param destination the result, written in place.
/// @param request the modulation and the mask.
/// @return success; `ErrorCode::OutOfRange` for a modulation outside the unit interval;
///         `ErrorCode::IncompatibleArguments` for a result of another width or height than the source, a result with
///         fewer channels than the source, or a mask of another width or height or of a channel count that weighs
///         neither one channel nor every one; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Status
blendProcessResult(const an::EngineContext& engine, const an::ExecutionContext& execution, const ProcessSource& source,
                   ProcessDestination& destination, const ProcessBlendRequest& request);

/// Blends an image a body handed over beside its result (`ProcessDestination::keepAlternative`) toward the source as
/// `blendProcessResult` blends the result, in place under its own write lease.
///
/// @param engine the engine context of the run's execution, for the working copies a conversion needs.
/// @param execution the run's admitted execution.
/// @param source what the run read.
/// @param alternative the image, which nothing else leases.
/// @param request the modulation and the mask.
/// @return success; `ErrorCode::NotInitialized` for an image that holds nothing; `ErrorCode::Busy` for one something
///         else leases; or what `blendProcessResult` refuses.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Status blendProcessAlternative(const an::EngineContext& engine,
                                                                        const an::ExecutionContext& execution,
                                                                        const ProcessSource& source,
                                                                        an::consumer::ImageAsset& alternative,
                                                                        const ProcessBlendRequest& request);

/// A rectangle of an image as a new asset of the run's session: the preview area's crop, through the engine's crop.
///
/// The crop is the engine's: its samples are the source's own, a mask the source states is cropped with them, the
/// solution is invalidated and the codec's cards dropped, as every crop's are.
///
/// @param session the session the crop belongs to.
/// @param engine the engine context of the run's execution.
/// @param execution the run's admitted execution.
/// @param image the image.
/// @param validity its packed mask, or null.
/// @param region the rectangle, in the image's pixels; it must lie inside the image.
/// @return the crop; `ErrorCode::InvalidArgument` for an empty rectangle or one that does not lie inside the image; or
///         what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
cropProcessRegion(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                  const an::ExecutionContext& execution, const an::consumer::AnyImage& image,
                  const an::ValidityMask* validity, const QRect& region);

/// What a preview area's composite base is described as: the patch's description at the whole image's width and
/// height, so the base and every patch pasted into it are one kind of image.
///
/// @param patch what the processed patch is.
/// @param image what the whole image is.
/// @param limits the limits of the engine context the base is made in.
/// @return the description, or nothing when the patch's description cannot be stated at the image's size.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::ImageDescriptor>
processCompositeBaseDescriptor(const an::ImageDescriptor& patch, const an::ImageDescriptor& image,
                               const an::EngineLimits& limits) noexcept;

/// The base a preview area's patches are pasted into: the whole image as `described`, a new asset of the run's session
/// carrying the image's metadata and validity. Built once per revision of the image and per description, and copied for
/// every composite.
///
/// A grey image under colour patches has its plane in every colour channel; a colour image under grey patches is its
/// luminance, with the Rec. 601 weights; an alpha channel the patches have and the image has not is opaque, and
/// one the image has and the patches have not is dropped. Another representation is converted at full scale.
///
/// @param session the session the base belongs to.
/// @param engine the engine context of the run's execution.
/// @param execution the run's admitted execution.
/// @param image the whole image.
/// @param validity its packed mask, or null.
/// @param described what the base is, from `processCompositeBaseDescriptor`.
/// @return the base; `ErrorCode::IncompatibleArguments` for another width or height or a channel change it cannot
///         make; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
buildProcessCompositeBase(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                          const an::ExecutionContext& execution, const an::consumer::AnyImage& image,
                          const an::ValidityMask* validity, const an::ImageDescriptor& described);

/// A copy of a base with a patch pasted into it at a position: the preview area's composite, a new asset of the run's
/// session. The paste is the engine's crop of the whole patch placed into the copy; a patch sample its own validity
/// rejects leaves the base's.
///
/// @param session the session the composite belongs to.
/// @param engine the engine context of the run's execution.
/// @param execution the run's admitted execution.
/// @param base the base, in the patch's format.
/// @param patch the processed patch.
/// @param patchValidity the patch's packed mask, or null.
/// @param origin where the patch's top left pixel goes in the base.
/// @return the composite; `ErrorCode::IncompatibleArguments` for a patch of another representation or channel count
///         than the base or one that does not lie inside it; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
composeProcessRegion(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                     const an::ExecutionContext& execution, const an::consumer::AnyImage& base,
                     const an::consumer::AnyImage& patch, const an::ValidityMask* patchValidity, QPoint origin);

} // namespace an::ui
