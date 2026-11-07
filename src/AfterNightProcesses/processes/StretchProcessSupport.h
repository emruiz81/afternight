// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the histogram transformation, curves, generalized hyperbolic and statistical stretch dialogs share: the working
/// image a stretch runs over and the way back to the image's own representation, the record a stretch leaves in an
/// image's metadata, the statistics of the whole image a stretch is decided from, and the rule that says whose
/// statistics those are.
///
/// **A stretch is decided from the whole image it changes.** The histogram transformation's automatic stretch, the
/// screen transfer the generalized hyperbolic dialog derives and the statistical stretch all read statistics of an
/// image: the target's for an Apply, a new image and a preview, and each frame's own for a sequence run, every frame
/// being an image the run changes. A preview area is a view of the target, so it is stretched with the target's
/// statistics - never the area's own - and shows exactly what Apply writes there. `StretchStatisticsCache` keeps a
/// measurement per revision of the image it describes, so a dialog measures its target once and every run over that
/// revision takes the measurement rather than taking it again; a revision it does not hold is measured where it is
/// needed.
///
/// **A stretch runs over a working image and is published in the image's own representation.** The engine's stretches
/// take Float32 or Float64 samples that are fractions of full scale: `makeStretchWorkingCopy` makes that copy and
/// `publishStretchResult` brings it back at full scale, to the nearest value, as a new image carrying the source's
/// validity and its metadata as the stretch leaves it - or, for a body that wrote its stretch where the run writes,
/// states that metadata on those samples where they are. `publishStretchOfSource` does both, and for a camera frame of
/// eight or sixteen bits stretched channel by channel it runs those steps over one row of every code instead and looks
/// each sample up in the answers - the same result, without the whole image's working copy.
///
/// **A dialog's background work takes its lease before its admission.** An Apply in place holds the target's writer
/// while it waits to be admitted, so work that held an admission while it waited for a read lease would wait for that
/// Apply and the Apply for it - and at a width of one nothing else would run. `admitDialogWork` takes the lease first.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "widgets/HistogramDisplayData.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QRect>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::ui::processes
{

/// Measurements of an image kept per revision, shared by a dialog and its runs.
///
/// The dialog stores what it measured of its target when the target changed, and a run looks the revision it reads up
/// before it measures anything itself. A measurement is found only for the revision it was taken of, so it is never
/// read for another image, or for another state of the same one. Holds the two latest revisions. Thread-safe.
template <typename ValueT> class StretchStatisticsCache
{
public:
    /// The measurement of a revision, or null when none is held.
    ///
    /// @param revision the revision of the image the measurement describes.
    /// @return the measurement, shared; null when this cache holds none of that revision.
    [[nodiscard]] std::shared_ptr<const ValueT> find(const an::consumer::AssetRevision& revision) const
    {
        const std::lock_guard lock(m_mutex);
        for (const Entry& entry : m_entries)
        {
            if (entry.revision == revision)
            {
                return entry.value;
            }
        }
        return nullptr;
    }

    /// Keeps a measurement of a revision, replacing the one held for it and letting the oldest go.
    ///
    /// @param revision the revision of the image it describes.
    /// @param value the measurement; null is not kept.
    void store(const an::consumer::AssetRevision& revision, std::shared_ptr<const ValueT> value)
    {
        if (!value)
        {
            return;
        }
        const std::lock_guard lock(m_mutex);
        for (Entry& entry : m_entries)
        {
            if (entry.revision == revision)
            {
                entry.value = std::move(value);
                return;
            }
        }
        if (m_entries.size() >= kCapacity)
        {
            m_entries.erase(m_entries.begin());
        }
        m_entries.push_back(Entry{.revision = revision, .value = std::move(value)});
    }

private:
    struct Entry
    {
        an::consumer::AssetRevision revision;
        std::shared_ptr<const ValueT> value;
    };

    static constexpr std::size_t kCapacity = 2u;

    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

/// The robust statistics a screen transfer is derived from.
struct ScreenTransferStatistics
{
    /// Each channel's median and median absolute deviation, as fractions of full scale, over the samples the image's
    /// validity admits.
    std::vector<an::StretchChannelStatistics> channels;
};

/// What a stretch records of itself in the image it wrote.
struct StretchRecord
{
    /// What the engine operation declares it does to an image.
    an::OperationEffect effect{};

    /// The engine entry point that wrote the samples, for the lineage.
    std::string operation;

    /// What the dialog asked of it, for the lineage.
    std::string detail;

    /// The name the processing-state cards record the stretch under.
    std::string processingOperation;
};

/// The validity an engine call reads an image's samples under: the rule its meaning states, and its packed mask when
/// that rule reads one.
///
/// @param metadata what the image means.
/// @param mask its packed mask, or null.
/// @param maskView where the mask's view is kept; the returned view points into it, so it must outlive the call that
///        reads the view.
/// @return the validity view.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::KernelValidityView
stretchValidity(const an::ImageMetadata& metadata, const an::ValidityMask* mask,
                std::optional<an::ValidityMaskView<const std::uint8_t>>& maskView);

/// A working copy of an image for a stretch to run over: Float64 for a Float64 image and for the 32-bit integers, whose
/// codes a single-precision fraction cannot hold, and Float32 for any other, in the image's own arrangement, its
/// samples read as `readAsFractions` reads them, whatever domain the image states - sensor codes and physical
/// quantities are read as the view shows them. It carries the image's metadata.
///
/// @param engine the engine context the copy belongs to.
/// @param execution the run's admitted execution.
/// @param image the image.
/// @return the copy; `ErrorCode::NotInitialized` for an image that holds nothing; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::AnyImage>
makeStretchWorkingCopy(const an::EngineContext& engine, const an::ExecutionContext& execution,
                       const an::consumer::AnyImage& image);

/// Runs the engine's stretch over a working image in place and leaves every channel the request does not change exactly
/// as it was. The engine holds every sample of a valid pixel to the unit interval and refuses one that is not a finite
/// number, in every channel; so the channels the request leaves alone - those an independent stretch does not enable,
/// and any channel past the third under a colour mode - are held aside in the engine's memory, set to zero for the
/// call and put back after it.
///
/// @param engine the engine context the held channels are allocated from.
/// @param execution the run's admitted execution.
/// @param working a floating working image, as `makeStretchWorkingCopy` makes one.
/// @param request the stretch.
/// @param validity which pixels the stretch changes.
/// @return success; `ErrorCode::Internal` for a working image that is not floating; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status stretchWorkingImage(const an::EngineContext& engine,
                                                                     const an::ExecutionContext& execution,
                                                                     an::consumer::AnyImage& working,
                                                                     const an::StretchImageRequest& request,
                                                                     const an::KernelValidityView& validity = {});

/// What an image's metadata becomes after a stretch: the source's acquisition record, solution and cards kept, its
/// meaning restated with no transfer function claimed and the validity it had, the stretch recorded in its lineage,
/// and the processing-state cards marking its samples nonlinear under the stretch's name.
///
/// @param metadata what the source's metadata is.
/// @param descriptor what the source is.
/// @param record what the stretch records of itself.
/// @return the metadata; or what the lineage, the semantics or the card bridge refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageMetadata>
stretchedMetadata(const an::ImageMetadata& metadata, const an::ImageDescriptor& descriptor,
                  const StretchRecord& record);

/// Makes the destination a stretched image: the samples of a working image - or of a rectangle of it - brought to the
/// source's representation at full scale, to the nearest value, with the source's validity and the metadata
/// `stretchedMetadata` states. When the working image is the destination's own - a body that wrote its stretch where
/// the run writes - its samples are the result: the metadata is stated on them where they are, in one transition,
/// and nothing of them is copied.
///
/// @param session the session the result belongs to.
/// @param engine the engine context of the run's execution.
/// @param execution the run's admitted execution.
/// @param source what the run read, whose description, metadata and validity the result takes.
/// @param working the stretched samples, of any representation, in one arrangement throughout; or the destination's
///        own image, holding the stretched samples in the source's representation.
/// @param from the rectangle of `working` the result is, of the source's size; empty when `working` is the source's
///        size and the result is all of it, as it must be when `working` is the destination's own image.
/// @param record what the stretch records of itself.
/// @param destination where the run writes, replaced by the result - or restated where it is, when `working` is its
///        own image.
/// @return success; `ErrorCode::IncompatibleArguments` when the working image or the rectangle is not of the source's
///         size, or a rectangle of the destination's own image is asked for; or what the metadata, the conversion or
///         the replacement refused. On a failure the destination is unchanged.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
publishStretchResult(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                     const an::ExecutionContext& execution, const ProcessSource& source,
                     const an::consumer::AnyImage& working, const QRect& from, const StretchRecord& record,
                     ProcessDestination& destination);

/// Whether one lookup of an image's codes answers a stretch of it exactly: the image unsigned eight- or sixteen-bit and
/// planar, every sample of it valid under its rule, of at least as many pixels as it has codes, and the stretch one of
/// each channel alone - linear, independent, or of a grey image - over at most four channels. Every step of the working
/// copy - the reading as fractions, the engine's stretch and the narrowing back - then takes a code through the same
/// arithmetic wherever it lies, the last partial vector of a row included, so the answer those steps give a code in a
/// row of every code is the answer they give it anywhere in the image.
///
/// @param image the image the stretch runs over.
/// @param request the stretch.
/// @param validity which pixels the stretch changes, as `stretchValidity` states them.
/// @return whether `publishStretchOfSource` looks the image's codes up.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool stretchLooksUpCodes(const an::consumer::AnyImage& image,
                                                               const an::StretchImageRequest& request,
                                                               const an::KernelValidityView& validity);

/// Stretches the source and makes the destination the result, exactly as a working copy stretched by
/// `stretchWorkingImage` and published by `publishStretchResult` does. Where `stretchLooksUpCodes` says so, the
/// working copy's own steps answer every code once, over one row of every code in every channel, and each sample of the
/// source is looked up in its channel's answers: one pass that reads the source and writes the result, in place of a
/// working copy of the whole image and the two passes over it. Otherwise the working copy runs.
///
/// @param session the session the result belongs to.
/// @param engine the engine context of the run's execution.
/// @param execution the run's admitted execution.
/// @param source what the run read, whose description, metadata and validity the result takes.
/// @param request the stretch.
/// @param validity which pixels the stretch changes, as `stretchValidity` states them.
/// @param record what the stretch records of itself.
/// @param destination where the run writes, replaced by the result.
/// @return success; or what the metadata, the stretch, the lookup or the replacement refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
publishStretchOfSource(const an::consumer::EngineSession& session, const an::EngineContext& engine,
                       const an::ExecutionContext& execution, const ProcessSource& source,
                       const an::StretchImageRequest& request, const an::KernelValidityView& validity,
                       const StretchRecord& record, ProcessDestination& destination);

/// Measures the statistics a screen transfer is derived from: each channel's median and median absolute deviation as
/// fractions of full scale, over the samples the image's validity admits - a floating image's as they are, an
/// integer's through its working copy.
///
/// @param engine the engine context of the execution.
/// @param execution the admitted execution the measurement runs under.
/// @param image the image.
/// @param mask its packed mask, or null.
/// @return the statistics; or what the engine refused, `ErrorCode::EmptySelection` when no sample is valid.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<ScreenTransferStatistics>
measureScreenTransferStatistics(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                const an::consumer::AnyImage& image, const an::ValidityMask* mask);

/// Whether a run reads a view of the target rather than the target: a preview, or the snapshot of one, issued while the
/// dialog showed an area, or a frame over a reduction or a region of the target (`ProcessRun::readsViewOfTarget`) -
/// the runs whose source is cut or reduced from the target rather than an image the run changes, and whose
/// measurement is the target's.
///
/// @param run what the run snapshotted.
/// @param area the preview area the dialog showed when the run was issued; empty for the whole image.
/// @return whether the run reads a view of the target.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool readsViewOfTarget(const ProcessRun& run, const QRect& area) noexcept;

/// Reads samples as the stretches take them, as fractions of full scale in Float32 or Float64: an unsigned integer
/// divided by its largest value, a signed one counted from its smallest value to its largest, a floating sample as it
/// is.
///
/// @param execution the run's admitted execution.
/// @param stored the samples' representation.
/// @param samples the samples.
/// @param layout their layout.
/// @param working Float32 or Float64, the fractions' representation.
/// @param to the fractions, written; of the samples' extent.
/// @param toLayout their layout.
/// @return success; or what the engine's conversion or scaling refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status readAsFractions(const an::ExecutionContext& execution,
                                                                 an::SampleType stored, const void* samples,
                                                                 const an::ImageLayout& layout, an::SampleType working,
                                                                 void* to, const an::ImageLayout& toLayout);

/// Writes fractions back to the representation they were read from, the way `readAsFractions` read it, to the nearest
/// value and saturated at the representation's range.
///
/// @param engine the engine context a signed integer's codes are worked out in.
/// @param execution the run's admitted execution.
/// @param stored the description of the samples written, whose representation they are written in.
/// @param working Float32 or Float64, the fractions' representation.
/// @param from the fractions.
/// @param fromLayout their layout.
/// @param to the samples, written.
/// @param toLayout their layout.
/// @return success; or what allocating, scaling or converting refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
writeFromFractions(const an::EngineContext& engine, const an::ExecutionContext& execution,
                   const an::ImageDescriptor& stored, an::SampleType working, const void* from,
                   const an::ImageLayout& fromLayout, void* to, const an::ImageLayout& toLayout);

/// What a dialog's background work holds while it runs: a read lease on the image it reads, when it reads one, and the
/// session's admission, released before the lease.
struct DialogWork
{
    std::optional<an::consumer::AssetReadLease> lease;
    an::consumer::OperationScope scope;
};

/// Admits a dialog's background work as a preview, taking the read lease on the image it reads before it asks to be
/// admitted: a writer the lease waits for is let go without an admission this work holds.
///
/// @param session the session that admits the work.
/// @param reading the image the work reads, or null when it reads none.
/// @return the lease and the admission; or what refused either.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<DialogWork> admitDialogWork(const an::consumer::EngineSession& session,
                                                                             const an::consumer::ImageAsset* reading);

/// The screen-transfer statistics a run is stretched with, under the whole-image rule: the target's for a run over a
/// view of it (`readsViewOfTarget`), the source's own otherwise - each looked up in the cache first and measured, and
/// kept, when it is not there. A sequence frame is measured and not kept.
///
/// @param engine the engine context of the run's execution.
/// @param execution the run's admitted execution.
/// @param source what the run reads.
/// @param run what the run snapshotted.
/// @param area the preview area the dialog showed when the run was issued.
/// @param target the image the dialog targets, as it was when the run was issued.
/// @param cache the dialog's measurements.
/// @return the statistics; `ErrorCode::Cancelled` when the run is over a view of the target and the target cannot be
///         read without waiting - a writer waits for it, so the preview is about to be superseded; or what the
///         measurement refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::shared_ptr<const ScreenTransferStatistics>>
screenTransferStatisticsFor(const an::EngineContext& engine, const an::ExecutionContext& execution,
                            const ProcessSource& source, const ProcessRun& run, const QRect& area,
                            const std::shared_ptr<const an::consumer::ImageAsset>& target,
                            StretchStatisticsCache<ScreenTransferStatistics>& cache);

/// One entry of the histogram dialogs' plot-resolution lists: the text it shows and the bins it names.
struct HistogramResolutionChoice
{
    /// The text the list shows.
    const char* label = "";

    /// The bins it names.
    an::ui::HistogramResolution resolution = an::ui::HistogramResolution::Bits8;
};

/// The plot resolutions the histogram transformation and generalized hyperbolic dialogs offer, in the order they are
/// shown.
///
/// @return the eleven entries, static.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::span<const HistogramResolutionChoice> histogramResolutionChoices() noexcept;

/// The resolution a plot-resolution list's text names.
///
/// @param label the text, as `histogramResolutionChoices` spells it.
/// @return the resolution, or nothing for a text the list does not show.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::ui::HistogramResolution>
histogramResolutionForLabel(const QString& label);

/// A histogram's counts with each changed channel remapped through its stretch: the histogram a dialog shows its result
/// as while the image is not stretched yet. Each count goes to the bin nearest where its bin's value is taken, through
/// the engine's `remapStretchHistogram`.
///
/// @param execution the admitted execution the remap runs under.
/// @param counts the image's counts.
/// @param stretches one stretch per channel, a screen transfer reading its first entry; a channel past the last takes
///        the last.
/// @param changed which channels the stretch changes; the others are kept as they are. A grey image's one channel is
///        always changed.
/// @return the remapped counts; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ui::HistogramDisplayData>
remapHistogramThroughStretch(const an::ExecutionContext& execution, const an::ui::HistogramDisplayData& counts,
                             std::span<const an::GeneralizedStretchParameters> stretches,
                             std::span<const bool> changed);

/// How many samples a screen transfer clips at either end, at a histogram's resolution.
struct ClippedSamples
{
    /// Samples in bins below the shadows point.
    std::uint64_t shadows = 0u;

    /// Samples in bins above the highlights point.
    std::uint64_t highlights = 0u;
};

/// The samples screen transfers clip, as the histogram transformation counts them: in each changed channel, the counts
/// of the bins below the shadows point's bin and above the highlights point's, each point's bin being its value times
/// the last bin's index, rounded to the nearest.
///
/// @param counts the image's counts.
/// @param transfers one transfer per channel; a channel past the last takes the last.
/// @param changed which channels the transfer changes; a grey image's one channel always is.
/// @return the clipped samples, summed over the changed channels.
[[nodiscard]] AFTERNIGHTPROCESSES_API ClippedSamples
clippedByScreenTransfer(const an::ui::HistogramDisplayData& counts,
                        std::span<const an::MidtonesTransferParameters> transfers, std::span<const bool> changed);

/// The colour component a stretch dialog's histogram shows beside an image's channels, as a fourth channel: the one a
/// colour stretch changes.
enum class HistogramComponent : std::uint8_t
{
    /// No fourth channel.
    None,

    /// CIE lightness, as a fraction of its range.
    Lightness,

    /// HSV saturation.
    Saturation,

    /// A luminance of stated weights.
    Luminance
};

/// Counts an image for a stretch dialog's histogram: every channel, over the samples its validity admits, and for a
/// colour image the component a colour stretch changes as a fourth channel - CIE lightness and HSV saturation through
/// the engine's channel scope, a luminance through a channel matrix over the image's working copy.
///
/// @param engine the engine context of the execution.
/// @param execution the admitted execution the count runs under.
/// @param image the image.
/// @param mask its packed mask, or null.
/// @param resolution how many bins.
/// @param component the fourth channel, or none; a grey image has none.
/// @param luminanceWeights the luminance's weights, summing to one; read only by `HistogramComponent::Luminance`.
/// @return the counts; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ui::HistogramDisplayData>
countStretchHistogram(const an::EngineContext& engine, const an::ExecutionContext& execution,
                      const an::consumer::AnyImage& image, const an::ValidityMask* mask,
                      an::ui::HistogramResolution resolution, HistogramComponent component,
                      const std::array<double, 3u>& luminanceWeights);

/// The mean of a rectangle of an image, its channels averaged, as a fraction of full scale: what a symmetry point
/// picked from the image is. The rectangle is cropped by the engine and read through its working copy.
///
/// @param session the session the crop belongs to.
/// @param engine the engine context of the execution.
/// @param execution the admitted execution.
/// @param image the image.
/// @param mask its packed mask, or null.
/// @param region the rectangle, in the image's pixels; a pixel for a point.
/// @return the mean; `ErrorCode::InvalidArgument` for a rectangle outside the image; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<double>
meanOfRegion(const an::consumer::EngineSession& session, const an::EngineContext& engine,
             const an::ExecutionContext& execution, const an::consumer::AnyImage& image, const an::ValidityMask* mask,
             const QRect& region);

} // namespace an::ui::processes
