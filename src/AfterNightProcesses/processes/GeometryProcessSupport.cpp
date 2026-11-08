// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The geometry steps, their metadata policy, the interpolation choices and the runs the geometry and filter-array
/// windows share.

#include "stdafx.hpp"

#include "processes/GeometryProcessSupport.h"

#include "app/AppContextHelpers.h"
#include "app/FileCompanions.h"
#include "app/PlateSolveTypes.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProcessCompanions.h"
#include "controls/ProgressDialogControl.h"
#include "controls/SequenceContainers.h"
#include "processes/MaskProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "session/ImageReads.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::ui::processes::GeometryStep;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)});
}

/// What a geometry step does to an image whose every sample it wrote from the source's: the engine's geometry effect,
/// with the source's validity rule kept.
constexpr an::OperationEffect kGeometryKeepsValidity{.description = an::DescriptionEffect::Restates,
                                                     .solution = an::SolutionEffect::Invalidates,
                                                     .codecMetadata = an::CodecMetadataEffect::Drops,
                                                     .outputIntent = an::OutputIntentEffect::Withdraws,
                                                     .validity = {.policy = an::ValidityPolicy::Preserve}};

/// Whether a step samples its source through an interpolation rather than copying or reducing stored values.
[[nodiscard]] bool samples(const GeometryStep& step) noexcept
{
    return std::holds_alternative<an::ResizeRequest>(step) || std::holds_alternative<an::RotationRequest>(step);
}

/// Whether samples of a representation and domain are floating fractions of full scale, which a sampled result holds
/// to the unit interval.
[[nodiscard]] bool heldToUnitInterval(const an::ImageDescriptor& descriptor) noexcept
{
    const bool floating =
        descriptor.sampleType() == an::SampleType::Float32 || descriptor.sampleType() == an::SampleType::Float64;
    const bool normalized = descriptor.numericDomain() == an::NumericDomain::NormalizedLinear ||
                            descriptor.numericDomain() == an::NumericDomain::EncodedNonlinear;
    return floating && normalized;
}

/// One step's result: the image and the mask it carries, when its rule needs one.
using Transformed = std::pair<an::consumer::AnyImage, std::optional<an::ValidityMask>>;

/// The cards a step's result carries: the source's observing context, the solution that followed the steps and the
/// source's solver card written beside it.
struct CarriedSolution
{
    /// The moved solution; nothing when the result carries none.
    std::optional<an::WcsSolution> solution;

    /// The source's card naming the solver that found the solution, when it had one.
    std::optional<an::CodecCard> solver;

    /// Where the telescope stood and pointed, which camera, optics and settings took the exposure, and when, as the
    /// engine carries a derived product's context (`an::withObservationCards`): a crop, a turn or a resize of a frame
    /// is still that frame, so a plate solve and the header still know its optics, its site and its target. A step that
    /// rescales the frame states its pixel size anew (`withPixelScale`).
    an::CodecMetadata observing;
};

/// The source's metadata with its cards replaced by the carried observing context, a carried solution's and the
/// solver's.
[[nodiscard]] an::Result<an::ImageMetadata> carriedMetadata(const an::ImageMetadata& source,
                                                            const CarriedSolution& carried)
{
    an::Result<an::CodecMetadata> codec = carried.observing;
    if (carried.solution.has_value())
    {
        codec = an::withWcsCards(carried.observing, *carried.solution);
        if (codec.hasValue() && carried.solver.has_value())
        {
            codec = codec->appended(*carried.solver);
        }
    }
    if (!codec.hasValue())
    {
        return codec.status();
    }
    return an::ImageMetadata::create(source.acquisition(), source.semantics(), source.astrometry(), source.provenance(),
                                     std::move(*codec), source.outputIntent());
}

/// The number a card's value states, blanks and one pair of quotes around it removed; nothing for any other text.
[[nodiscard]] std::optional<double> cardNumber(std::string value)
{
    const auto trimmed = [](const std::string& text) {
        const std::size_t first = text.find_first_not_of(" \t");
        return first == std::string::npos ? std::string()
                                          : text.substr(first, text.find_last_not_of(" \t") - first + 1u);
    };
    value = trimmed(value);
    if (value.size() >= 2u && value.front() == '\'' && value.back() == '\'')
    {
        value = trimmed(value.substr(1u, value.size() - 2u));
    }
    bool parsed = false;
    const double number = QString::fromStdString(value).toDouble(&parsed);
    return parsed && std::isfinite(number) && number > 0.0 ? std::optional<double>(number) : std::nullopt;
}

/// The observing context of a frame rescaled by a step: each pixel-size card divided by the step's scale along its
/// axis, so the plate scale the optics imply is still the frame's own. A card that states no size is kept as it is.
[[nodiscard]] an::Result<an::CodecMetadata> withPixelScale(const an::CodecMetadata& observing, const double scaleX,
                                                           const double scaleY)
{
    std::vector<an::CodecCard> restated;
    restated.reserve(observing.cards().size());
    for (const an::CodecCard& card : observing.cards())
    {
        const bool horizontal = card.keyword() == "XPIXSZ";
        const bool vertical = card.keyword() == "YPIXSZ";
        const std::optional<double> size = card.origin() == an::CodecNamespace::Fits && (horizontal || vertical)
                                               ? cardNumber(card.value())
                                               : std::nullopt;
        if (!size.has_value())
        {
            restated.push_back(card);
            continue;
        }
        an::Result<an::CodecCard> scaled =
            an::CodecCard::create(an::CodecNamespace::Fits, card.keyword(),
                                  fmt::format("{:.6g}", *size / (horizontal ? scaleX : scaleY)), card.comment());
        if (!scaled.hasValue())
        {
            return scaled.status();
        }
        restated.push_back(std::move(*scaled));
    }
    return an::CodecMetadata::create(observing.sourceEncoding(), std::move(restated));
}

/// Whether a step changes the frame's scale rather than cutting or turning it.
[[nodiscard]] bool rescales(const GeometryStep& step) noexcept
{
    return std::holds_alternative<an::ResizeRequest>(step) ||
           std::holds_alternative<an::IntegerResampleRequest>(step) ||
           std::holds_alternative<an::ui::processes::CfaIntegerResampleStep>(step);
}

/// The scale a rescaling step changes the frame by along each axis: an integer resample's exact factor, enlarging or
/// reducing, whatever remainder its blocks leave at an edge, and a resize's sides over the source's.
[[nodiscard]] std::pair<double, double> stepScale(const GeometryStep& step, const an::ImageDescriptor& source,
                                                  const an::ImageDescriptor& result) noexcept
{
    const auto integer = [](const an::IntegerResampleRequest& request) {
        const double factor = static_cast<double>(request.factor);
        const double scale = request.direction == an::IntegerResampleDirection::Enlarge ? factor : 1.0 / factor;
        return std::pair{scale, scale};
    };
    if (const auto* resample = std::get_if<an::IntegerResampleRequest>(&step); resample != nullptr)
    {
        return integer(*resample);
    }
    if (const auto* mosaic = std::get_if<an::ui::processes::CfaIntegerResampleStep>(&step); mosaic != nullptr)
    {
        return integer(mosaic->request);
    }
    return {static_cast<double>(result.extent().width()) / static_cast<double>(source.extent().width()),
            static_cast<double>(result.extent().height()) / static_cast<double>(source.extent().height())};
}

/// The solution an image's cards state, and its solver's card; nothing for an image whose cards state none.
[[nodiscard]] CarriedSolution solutionOf(const an::ImageMetadata& metadata, const an::ImageDescriptor& described)
{
    CarriedSolution found;
    const std::span<const an::CodecCard> cards = metadata.codec().cards();
    an::Result<an::WcsSolution> solution =
        an::ui::readImageWcsSolution(cards, described.extent().width(), described.extent().height());
    if (!solution.hasValue())
    {
        return found;
    }
    found.solution.emplace(std::move(*solution));
    for (const an::CodecCard& card : cards)
    {
        if (card.keyword() == an::ui::kPlateSolveSolverKeyword)
        {
            found.solver = card;
        }
    }
    return found;
}

/// What a step's result is described as: the plan's destination, with a resize's pixel aspect the source's own.
///
/// A resize is planned under the aspect that leaves the result's pixels the source's, and the engine multiplies that
/// aspect back by the scales; the round trip can land an ulp or two away from where it started, which a rotation, which
/// takes only square pixels, then refuses. The source's aspect is stated exactly whenever the planned one is that
/// close to it; anything further away is the engine's answer and stands.
[[nodiscard]] an::ImageDescriptor stepResultDescriptor(const an::ImageDescriptor& source, const an::GeometryPlan& plan,
                                                       const GeometryStep& step)
{
    const an::ImageDescriptor& planned = plan.destinationDescriptor();
    const double kept = source.pixelAspectRatio();
    const double stated = planned.pixelAspectRatio();
    constexpr double kRoundingTolerance = 16.0 * std::numeric_limits<double>::epsilon();
    if (!std::holds_alternative<an::ResizeRequest>(step) || stated == kept ||
        std::abs(stated - kept) > kRoundingTolerance * kept)
    {
        return planned;
    }
    an::ImageDescriptorRequest request = planned.toRequest();
    request.pixelAspectRatio = kept;
    const std::optional<an::ImageDescriptor> exact = an::ImageDescriptor::create(request);
    return exact.has_value() ? *exact : planned;
}

/// Runs one planned step over a typed image and its explicit mask under an admitted operation, into an image
/// described as `described`, which lays its samples out as the plan's destination does.
template <typename SampleT>
[[nodiscard]] an::Result<Transformed>
transformTyped(const an::ui::ExecutionScope& scope, const an::Image<SampleT>& image, const an::ValidityMask* validity,
               const an::GeometryPlan& plan, const an::ImageDescriptor& described, bool holdToUnit,
               std::string_view lineage, const CarriedSolution& solution)
{
    const auto view = image.view();
    if (!view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    // A result holds the source's observing context, and the solution's cards and the solver's when it carries a
    // solution; no other card of the source describes it.
    an::Result<an::ImageMetadata> startingMetadata = carriedMetadata(image.metadata(), solution);
    if (!startingMetadata.hasValue())
    {
        return startingMetadata.status();
    }
    const bool carriesCards = !startingMetadata->codec().cards().empty();
    an::Result<an::Image<SampleT>> result =
        an::Image<SampleT>::create(scope.engine(), described, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Zeroed, *startingMetadata);
    if (!result.hasValue())
    {
        return result.status();
    }
    an::Result<an::ValidityMask> written = an::ValidityMask::create(scope.engine(), described.extent());
    if (!written.hasValue())
    {
        return written.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
    if (!writtenView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the result's mask holds nothing");
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask;
    if (validity != nullptr)
    {
        sourceMask = validity->view();
    }
    an::GeometrySelection selection;
    selection.validity = image.metadata().semantics().validity();
    selection.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
    std::uint64_t outputsWritten = 0u;
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = result->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
        }
        const an::Result<an::GeometryResult> ran =
            an::transformImageGeometry(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                       access->view().data(), access->view().layout(), plan, selection, *writtenView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
        outputsWritten = ran->run.outputsWritten;
        if (holdToUnit && heldToUnitInterval(described))
        {
            const an::ImageLayout layout = access->view().layout();
            if (an::Status held = process_utils::scaleAndBiasInPlace(scope.execution(), an::kSampleTypeOf<SampleT>,
                                                                     access->view().data(), layout, 1.0, 0.0,
                                                                     an::ClampInterval{.low = 0.0, .high = 1.0});
                held.failed())
            {
                return held;
            }
        }
    }
    const an::ImageExtent& extent = described.extent();
    const std::uint64_t samplesInResult =
        static_cast<std::uint64_t>(extent.width()) * extent.height() * extent.channelCount();
    // A source that states its validity in a mask keeps that rule with the mask the step moved; one whose every result
    // sample the step wrote keeps its own rule; and a step that left some sample unwritten states its validity in the
    // mask it wrote, which calls that sample invalid.
    const bool everyWritten = outputsWritten == samplesInResult;
    const bool carriesMask = validity != nullptr || !everyWritten;
    an::OperationEffect effect = kGeometryKeepsValidity;
    an::OperationOutcome outcome{.operation = "an::transformImageGeometry", .detail = std::string(lineage)};
    if (validity == nullptr && !everyWritten)
    {
        an::ImageSemanticsRequest meaning = image.metadata().semantics().toRequest();
        meaning.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
        an::Result<an::ImageSemantics> restated = an::ImageSemantics::create(meaning);
        if (!restated.hasValue())
        {
            return restated.status();
        }
        effect = an::kGeometryEffect;
        effect.semantics = an::SemanticEffect::Restates;
        outcome.semantics = std::move(*restated);
    }
    an::Result<an::MetadataTransition> change = an::operationTransition(effect, outcome);
    if (!change.hasValue())
    {
        return change.status();
    }
    if (solution.solution.has_value())
    {
        an::Result<an::AstrometricSolution> summary = solution.solution->toAstrometricSolution();
        if (!summary.hasValue())
        {
            return summary.status();
        }
        change->astrometry = std::move(*summary);
    }
    if (carriesCards)
    {
        change->clearCodecMetadata = false;
    }
    if (an::Status stated = result->transition(*change); stated.failed())
    {
        return stated;
    }
    std::optional<an::ValidityMask> carried;
    if (carriesMask)
    {
        carried.emplace(std::move(*written));
    }
    return Transformed{an::consumer::AnyImage(std::move(*result)), std::move(carried)};
}

/// The names of the interpolations the dialogs offer after Auto, in their order.
[[nodiscard]] const std::array<const QString*, 7u>& namedInterpolations()
{
    static const std::array<const QString*, 7u> names{&process_utils::INTERP_NEAREST_NEIGHBOR,
                                                      &process_utils::INTERP_BILINEAR,
                                                      &process_utils::INTERP_BICUBIC,
                                                      &process_utils::INTERP_AREA,
                                                      &process_utils::INTERP_LANCZOS4,
                                                      &process_utils::INTERP_BIT_EXACT_BILINEAR,
                                                      &process_utils::INTERP_BIT_EXACT_NEAREST_NEIGHBOR};
    return names;
}

/// Runs work on a worker and its completion on the GUI thread; the completion is dropped when the application is gone.
template <typename WorkT, typename DoneT> void runThenLand(WorkT work, DoneT done)
{
    QThreadPool::globalInstance()->start([work = std::move(work), done = std::move(done)]() mutable {
        auto result = std::make_shared<decltype(work())>(work());
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application, [result, done]() mutable { done(std::move(*result)); }, Qt::QueuedConnection);
    });
}

/// Moves a progress window to a fraction of a run, from a worker.
void postProgress(const QPointer<ProgressDialogControl>& progress, std::uint64_t done, std::uint64_t total)
{
    QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr || total == 0u)
    {
        return;
    }
    const int percent = static_cast<int>((done * 100u) / total);
    QMetaObject::invokeMethod(
        application,
        [progress, percent]() {
            if (progress)
            {
                progress->setValue(percent);
            }
        },
        Qt::QueuedConnection);
}

/// Admits a run's operation under an observer - the progress window's, when the run shows one - and tells the hook.
[[nodiscard]] an::Result<an::ui::ExecutionScope>
admitRun(const an::consumer::EngineSession& session, std::stop_token stop, const an::ui::processes::AdmissionHook& hook,
         an::ui::ExecutionObserver observer = {}, const an::ui::ImageReads* reads = nullptr)
{
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session,
        an::ui::ExecutionRequest{
            .purpose = an::consumer::OperationPurpose::Apply, .stopToken = std::move(stop), .reads = reads},
        std::move(observer));
    if (scope.hasValue() && hook)
    {
        hook(*scope);
    }
    return scope;
}

/// The images a run's request names, to be leased before it is admitted.
[[nodiscard]] std::shared_ptr<an::ui::ImageReads>
readsOf(const std::vector<std::shared_ptr<const an::consumer::ImageAsset>>& images)
{
    auto reads = std::make_shared<an::ui::ImageReads>();
    for (const std::shared_ptr<const an::consumer::ImageAsset>& image : images)
    {
        reads->name(image);
    }
    return reads;
}

/// Lets a run's leases go when its worker is done with them, however it ends.
class ReadsRelease
{
public:
    explicit ReadsRelease(an::ui::ImageReads& reads) noexcept : m_reads(reads) {}
    ~ReadsRelease()
    {
        m_reads.release();
    }
    ReadsRelease(const ReadsRelease&) = delete;
    ReadsRelease& operator=(const ReadsRelease&) = delete;

private:
    an::ui::ImageReads& m_reads;
};

/// What a run over a sequence came to: one sequence per output, published whole, and the image each new document
/// shows - the frame the source showed, or the first.
struct SequenceRun
{
    an::Status status = an::Status::success();
    std::vector<an::ui::DocumentSequence> sequences;
    std::vector<an::consumer::ImageAsset> shown;
};

/// Reads the frames of a sequence, runs an operation over each - or takes each as it is when there is none - and
/// writes what it makes into new sequences, one per path, each published whole or not at all. A worker.
[[nodiscard]] SequenceRun runOverFrames(const an::consumer::EngineSession& session, an::ui::DocumentSequence sequence,
                                        bool readDemosaiced, const std::vector<QString>& paths, std::stop_token stop,
                                        const QPointer<ProgressDialogControl>& progress,
                                        const an::ui::processes::ImagesOperation& operation,
                                        const an::ui::processes::AdmissionHook& hook, an::ui::ImageReads& reads)
{
    SequenceRun run;
    // The images the operation reads beside the frames are leased before the run is admitted: an edit of a view holds
    // its writer while it waits to be admitted.
    const an::Status taken = reads.take(stop);
    const ReadsRelease letGo(reads);
    if (taken.failed())
    {
        run.status = taken;
        return run;
    }
    an::Result<an::ui::ExecutionScope> scope = admitRun(session, stop, hook, {}, &reads);
    if (!scope.hasValue())
    {
        run.status = scope.status();
        return run;
    }
    const std::uint64_t shownFrame = sequence.currentFrame;
    const auto body = [&]() -> an::Status {
        an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
            an::ui::openProcessFrames(session, sequence, readDemosaiced);
        if (!reader.hasValue())
        {
            return reader.status();
        }
        std::vector<std::unique_ptr<an::ui::IProcessFrameWriter>> writers;
        for (const QString& path : paths)
        {
            an::Result<std::unique_ptr<an::ui::IProcessFrameWriter>> writer =
                an::ui::createProcessFrameOutput(session, sequence, path);
            if (!writer.hasValue())
            {
                return writer.status();
            }
            writers.push_back(std::move(*writer));
        }
        const std::uint64_t total = (*reader)->frameCount();
        std::uint64_t written = 0u;
        std::uint64_t shownWritten = 0u;
        bool shownFound = false;
        for (;;)
        {
            if (scope->stopRequested())
            {
                return refusal(an::ErrorCode::Cancelled, "the run was stopped");
            }
            an::Result<std::optional<an::ui::ProcessSequenceFrame>> frame =
                (*reader)->next(scope->engine(), scope->execution());
            if (!frame.hasValue())
            {
                return frame.status();
            }
            if (!frame->has_value())
            {
                break;
            }
            std::vector<an::consumer::ImageAsset> made;
            if (operation)
            {
                an::Result<an::consumer::AssetReadLease> lease = (**frame).asset.read(stop);
                if (!lease.hasValue())
                {
                    return lease.status();
                }
                an::Result<std::vector<an::consumer::ImageAsset>> images =
                    operation(*scope, an::ui::ProcessSource::of(*lease));
                if (!images.hasValue())
                {
                    return images.status();
                }
                made = std::move(*images);
            }
            else
            {
                made.push_back(std::move((**frame).asset));
            }
            if (made.size() != writers.size())
            {
                return refusal(an::ErrorCode::Internal, "a frame made another number of images than sequences");
            }
            for (std::size_t output = 0u; output < writers.size(); ++output)
            {
                an::Result<an::consumer::AssetReadLease> image = made[output].read(stop);
                if (!image.hasValue())
                {
                    return image.status();
                }
                if (an::Status appended =
                        writers[output]->write(scope->engine(), scope->execution(), (**frame).index, image->image());
                    appended.failed())
                {
                    return appended;
                }
            }
            if (run.shown.empty() || (!shownFound && (**frame).index == shownFrame))
            {
                shownFound = shownFound || (**frame).index == shownFrame;
                run.shown = std::move(made);
                shownWritten = written;
            }
            ++written;
            postProgress(progress, written, total);
        }
        if (written == 0u)
        {
            return refusal(an::ErrorCode::EmptySelection, "the sequence held no frame");
        }
        for (const std::unique_ptr<an::ui::IProcessFrameWriter>& writer : writers)
        {
            an::Result<an::ui::DocumentSequence> finished = writer->finish(scope->engine(), scope->execution());
            if (!finished.hasValue())
            {
                return finished.status();
            }
            finished->currentFrame = shownWritten;
            run.sequences.push_back(std::move(*finished));
        }
        return an::Status::success();
    };
    run.status = body();
    if (run.status.failed())
    {
        run.shown.clear();
        run.sequences.clear();
    }
    scope->finish(run.status);
    return run;
}

/// Removes the sequences a run wrote from the path at an index on: those no view shows.
void removeSequences(const std::vector<QString>& paths, std::size_t from)
{
    for (std::size_t index = from; index < paths.size(); ++index)
    {
        if (an::Status removed = an::ui::removeSequenceFromDisk(paths[index]); removed.failed())
        {
            LOG_WARNING("Could not remove the sequence '{}': {}", paths[index].toStdString(),
                        std::string(removed.message()));
        }
    }
}

/// New temporary paths in a sequence container, or nothing when the application lends no temporary files.
[[nodiscard]] std::optional<std::vector<QString>> temporaryPaths(an::SequenceFormat format, std::size_t count)
{
    std::vector<QString> paths;
    for (std::size_t index = 0u; index < count; ++index)
    {
        const std::string path = process_utils::createTempSequenceFilePath(format);
        if (path.empty())
        {
            return std::nullopt;
        }
        paths.push_back(QString::fromStdString(path));
    }
    return paths;
}

/// The progress window of a run over a sequence, shown; its Cancel stops the run.
[[nodiscard]] ProgressDialogControl* showProgress(const an::ui::processes::GeometryRunWords& words)
{
    auto* progress = new ProgressDialogControl(process_utils::currentMainWindow(), words.title);
    progress->setText(words.progressText);
    progress->setProgressBarRange(0, 100);
    progress->show();
    return progress;
}

/// The sequence a run reads: every frame, or the frames it names, starting at the frame the view shows.
[[nodiscard]] an::ui::DocumentSequence sequenceToRead(an::ui::DocumentSequence sequence,
                                                      const std::optional<std::vector<std::uint64_t>>& frames)
{
    sequence.selectedFrames = frames;
    return sequence;
}

} // namespace

namespace an::ui::processes
{

// ---- The steps.

an::Result<an::GeometryPlan> planGeometryStep(const an::ImageDescriptor& source, const GeometryStep& step)
{
    return std::visit(
        [&source](const auto& request) -> an::Result<an::GeometryPlan> {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<Request, an::CropRequest>)
            {
                return an::planCrop(source, request);
            }
            else if constexpr (std::is_same_v<Request, an::ResizeRequest>)
            {
                // The engine states a resize that scales the axes differently as stretched pixels; the product's pixels
                // are square, and a resize decides a new grid of them - so the source is planned under the aspect that
                // leaves the result's pixels the source's, which a rotation accepts.
                if (request.width == 0u || request.height == 0u)
                {
                    return an::planResize(source, request);
                }
                const double sx = static_cast<double>(source.extent().width()) / request.width;
                const double sy = static_cast<double>(source.extent().height()) / request.height;
                an::ImageDescriptorRequest kept = source.toRequest();
                kept.pixelAspectRatio = source.pixelAspectRatio() * sy / sx;
                const std::optional<an::ImageDescriptor> planned = an::ImageDescriptor::create(kept);
                if (!planned.has_value())
                {
                    return refusal(an::ErrorCode::IncompatibleArguments, "the image cannot be resized to that size");
                }
                return an::planResize(*planned, request);
            }
            else if constexpr (std::is_same_v<Request, an::RotationRequest>)
            {
                return an::planRotation(source, request);
            }
            else if constexpr (std::is_same_v<Request, an::IntegerResampleRequest>)
            {
                return an::planIntegerResample(source, request);
            }
            else
            {
                return an::planCfaIntegerResample(source, request.request);
            }
        },
        step);
}

an::Result<an::ImageDescriptor> geometryResultDescriptor(const an::ImageDescriptor& source,
                                                         std::span<const GeometryStep> steps)
{
    an::ImageDescriptor current = source;
    for (const GeometryStep& step : steps)
    {
        const an::Result<an::GeometryPlan> plan = planGeometryStep(current, step);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        current = stepResultDescriptor(current, *plan, step);
    }
    return current;
}

an::Result<std::optional<an::WcsSolution>> movedSolution(const an::WcsSolution& solution, const an::GeometryPlan& plan,
                                                         const GeometryStep& step)
{
    using Moved = an::Result<std::optional<an::WcsSolution>>;
    const auto carried = [](an::Result<an::WcsSolution> moved) -> Moved {
        if (!moved.hasValue())
        {
            return moved.status();
        }
        return std::optional<an::WcsSolution>(std::move(*moved));
    };
    const an::ImageExtent& from = plan.sourceDescriptor().extent();
    const an::ImageExtent& to = plan.destinationDescriptor().extent();
    return std::visit(
        [&](const auto& request) -> Moved {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<Request, an::CropRequest>)
            {
                return carried(an::cropWcsSolution(solution, plan.cropX(), plan.cropY(), to.width(), to.height()));
            }
            else if constexpr (std::is_same_v<Request, an::ResizeRequest>)
            {
                const double scaleX = static_cast<double>(to.width()) / from.width();
                const double scaleY = static_cast<double>(to.height()) / from.height();
                an::Result<an::WcsSolution> resized =
                    an::resizeWcsSolution(solution, scaleX, scaleY, to.width(), to.height());
                if (!resized.hasValue() && resized.status().code() == an::ErrorCode::Unsupported)
                {
                    // The axes scale further apart than a distortion is rescaled across, and it no longer describes
                    // the frame.
                    resized =
                        an::resizeWcsSolution(solution.withoutDistortion(), scaleX, scaleY, to.width(), to.height());
                }
                return carried(std::move(resized));
            }
            else if constexpr (std::is_same_v<Request, an::IntegerResampleRequest>)
            {
                const double factor = static_cast<double>(request.factor);
                const double scale = request.direction == an::IntegerResampleDirection::Enlarge ? factor : 1.0 / factor;
                return carried(an::resizeWcsSolution(solution, scale, scale, to.width(), to.height()));
            }
            else
            {
                return std::optional<an::WcsSolution>();
            }
        },
        step);
}

an::Result<an::consumer::ImageAsset> transformImage(const an::ui::ExecutionScope& scope,
                                                    const an::ui::ProcessSource& source,
                                                    std::span<const GeometryStep> steps, std::string_view lineage,
                                                    bool mosaicAsGrey)
{
    if (steps.empty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a geometry run needs a step");
    }
    std::optional<Transformed> current;
    CarriedSolution carried;
    if (const std::optional<an::ImageDescriptor> described = source.descriptor(); described.has_value())
    {
        carried = solutionOf(source.metadata(), *described);
    }
    if (an::Result<an::CodecMetadata> observing = an::ui::derivedFrameCards(source.metadata().codec());
        observing.hasValue())
    {
        carried.observing = std::move(*observing);
    }
    else
    {
        LOG_WARNING("The observing context of the image could not follow its {}, so it was dropped: {}",
                    std::string(lineage), std::string(observing.status().message()));
    }
    for (const GeometryStep& step : steps)
    {
        const an::consumer::AnyImage& image = current.has_value() ? current->first : source.image();
        const an::ValidityMask* validity =
            current.has_value() ? (current->second.has_value() ? &*current->second : nullptr) : source.validity();
        an::Result<Transformed> next = std::visit(
            [&](const auto& typed) -> an::Result<Transformed> {
                std::optional<an::ImageDescriptor> described = typed.descriptor();
                if (!described.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
                }
                if (mosaicAsGrey && described->isMosaiced())
                {
                    // The samples are laid out as they are; only what the plan is told they measured changes.
                    an::ImageDescriptorRequest grey = described->toRequest();
                    grey.cfa = an::CfaDescriptor{};
                    described = an::ImageDescriptor::create(grey);
                    if (!described.has_value())
                    {
                        return refusal(an::ErrorCode::IncompatibleArguments, "the mosaic cannot be read as grey");
                    }
                }
                const an::Result<an::GeometryPlan> plan = planGeometryStep(*described, step);
                if (!plan.hasValue())
                {
                    return plan.status();
                }
                if (carried.solution.has_value())
                {
                    an::Result<std::optional<an::WcsSolution>> moved = movedSolution(*carried.solution, *plan, step);
                    if (!moved.hasValue())
                    {
                        LOG_WARNING("The solution of the image could not follow its {}, so it was dropped: {}",
                                    std::string(lineage), std::string(moved.status().message()));
                    }
                    carried.solution = moved.hasValue() ? std::move(*moved) : std::nullopt;
                }
                const an::ImageDescriptor resulting = stepResultDescriptor(*described, *plan, step);
                if (rescales(step))
                {
                    const std::pair<double, double> scale = stepScale(step, *described, resulting);
                    an::Result<an::CodecMetadata> rescaled =
                        withPixelScale(carried.observing, scale.first, scale.second);
                    if (!rescaled.hasValue())
                    {
                        return rescaled.status();
                    }
                    carried.observing = std::move(*rescaled);
                }
                return transformTyped(scope, typed, validity, *plan, resulting, samples(step), lineage, carried);
            },
            image);
        if (!next.hasValue())
        {
            return next.status();
        }
        current.emplace(std::move(*next));
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(current->first), std::move(current->second));
}

// ---- The interpolation.

QString resizeAutoInterpolationText()
{
    return QStringLiteral("Auto (Lanczos4 to enlarge, Pixel Area Relation to reduce)");
}

QString rotationAutoInterpolationText()
{
    return QStringLiteral("Auto (Lanczos4)");
}

void fillInterpolationChoices(QComboBox* combo, const QString& autoText)
{
    combo->clear();
    combo->addItem(autoText, process_utils::INTERP_AUTO);
    for (const QString* name : namedInterpolations())
    {
        combo->addItem(*name, *name);
    }
}

QString selectedInterpolationName(const QComboBox* combo)
{
    const QVariant data = combo->currentData();
    return data.isValid() ? data.toString() : combo->currentText();
}

void selectInterpolationName(QComboBox* combo, const QString& name)
{
    const int index = combo->findData(name);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
}

ResolvedInterpolation resolveResizeInterpolation(const QString& name, bool enlarges)
{
    if (name == process_utils::INTERP_AUTO)
    {
        return enlarges ? ResolvedInterpolation{an::KernelInterpolation::Lanczos4, process_utils::INTERP_LANCZOS4, true}
                        : ResolvedInterpolation{an::KernelInterpolation::Area, process_utils::INTERP_AREA, true};
    }
    const process_utils::InterpolationChoice chosen = process_utils::interpolationFromLabel(name);
    return {chosen.interpolation, chosen.known ? name : process_utils::INTERP_LANCZOS4, false};
}

ResolvedInterpolation resolveRotationInterpolation(const QString& name)
{
    if (name == process_utils::INTERP_AUTO)
    {
        return {an::KernelInterpolation::Lanczos4, process_utils::INTERP_LANCZOS4, true};
    }
    const process_utils::InterpolationChoice chosen = process_utils::interpolationFromLabel(name);
    return {chosen.interpolation, chosen.known ? name : process_utils::INTERP_LANCZOS4, false};
}

an::ResizeRequest resizeRequest(std::uint32_t width, std::uint32_t height, an::KernelInterpolation interpolation)
{
    return an::ResizeRequest{.width = width,
                             .height = height,
                             .sampling = {.interpolation = interpolation,
                                          .border = an::KernelBorderMode::Clamp,
                                          .invalid = an::SamplingInvalidPolicy::RequireAll},
                             .ringing = an::ResizeRingingPolicy::SuppressDarkRinging};
}

an::RotationRequest rotationRequest(double degrees, bool expandCanvas, an::KernelInterpolation interpolation)
{
    return an::RotationRequest{.angleRadians = degrees * std::numbers::pi / 180.0,
                               .canvas = expandCanvas ? an::RotationCanvas::ExpandToFit : an::RotationCanvas::Preserve,
                               .sampling = {.interpolation = interpolation,
                                            .border = an::KernelBorderMode::Constant,
                                            .constantBorder = 0.0f,
                                            .invalid = an::SamplingInvalidPolicy::RequireAll}};
}

// ---- The reducers.

const std::vector<QString>& integerReductionLabels()
{
    static const std::vector<QString> labels{QStringLiteral("Average"), QStringLiteral("Median"),
                                             QStringLiteral("Maximum"), QStringLiteral("Minimum")};
    return labels;
}

an::IntegerReduction integerReductionFromLabel(const QString& label)
{
    if (label == QStringLiteral("Median"))
    {
        return an::IntegerReduction::Median;
    }
    if (label == QStringLiteral("Maximum"))
    {
        return an::IntegerReduction::Maximum;
    }
    if (label == QStringLiteral("Minimum"))
    {
        return an::IntegerReduction::Minimum;
    }
    return an::IntegerReduction::Average;
}

an::IntegerResampleRequest integerResampleRequest(std::uint32_t factor, bool enlarge, an::IntegerReduction reduction)
{
    return an::IntegerResampleRequest{.factor = factor,
                                      .direction = enlarge ? an::IntegerResampleDirection::Enlarge
                                                           : an::IntegerResampleDirection::Reduce,
                                      .reduction = reduction,
                                      .remainder = an::IntegerRemainderPolicy::DiscardPartialEdges};
}

// ---- Sizes.

std::uint64_t imageBytesOf(const an::ImageDescriptor& descriptor) noexcept
{
    const an::ImageExtent& extent = descriptor.extent();
    return static_cast<std::uint64_t>(extent.width()) * extent.height() * extent.channelCount() *
           an::sampleTypeSizeBytes(descriptor.sampleType());
}

QString sizeText(double bytes)
{
    double size = bytes / 1048576.0;
    if (size > 1024.0)
    {
        return QStringLiteral("%1 GB").arg(size / 1024.0, 0, 'f', 2);
    }
    return QStringLiteral("%1 MB").arg(size, 0, 'f', 2);
}

// ---- The runs.

const an::consumer::EngineSession* lentEngineSession()
{
    if (an::ui::IEngineSessionProvider* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

void reportRunFailure(QWidget* parent, const GeometryRunWords& words, const an::Status& status)
{
    if (status.ok() || status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::Busy)
    {
        return;
    }
    const std::string text = fmt::format("{}:\n{}", words.errorLead, status.message());
    LOG_WARNING("{}", text);
    process_utils::notifyErrorMessage(parent, words.errorTitle, QString::fromStdString(text));
}

void runInPlace(ImageViewControl& view, InPlaceRunRequest request, GeometryRunDone done)
{
    const an::consumer::EngineSession* session = lentEngineSession();
    if (session == nullptr)
    {
        const an::Status none = refusal(an::ErrorCode::NotInitialized, "no engine session is lent");
        reportRunFailure(nullptr, request.words, none);
        if (done)
        {
            done(GeometryRunOutcome{.status = none});
        }
        return;
    }
    if (!view.isSequence())
    {
        auto failure = std::make_shared<std::optional<an::Status>>();
        // The window asked for: its Cancel stops the run, its observer shows the operation's reports, and the edit's
        // own stop - a document changed or closed under it - reaches the operation through the scope.
        ProgressDialogControl* progress = request.progressWindow ? showProgress(request.words) : nullptr;
        const QPointer<ProgressDialogControl> progressGuard(progress);
        const std::optional<std::stop_token> windowStop =
            progress != nullptr ? std::optional<std::stop_token>(progress->stopToken()) : std::nullopt;
        an::ui::ExecutionObserver observer = progress != nullptr ? progress->observer() : an::ui::ExecutionObserver{};
        // The images the operation reads beside the view's own are leased by the edit, with the view's writer and in
        // one order, before the operation asks to be admitted.
        std::shared_ptr<an::ui::ImageReads> reads = readsOf(request.reads);
        view.editImage(
            request.stepName, request.stepDescription,
            [operation = request.operation, hook = request.admissionHook, inFlight = request.inFlight, failure,
             windowStop, observer,
             reads](const an::consumer::EngineSession& editSession, const an::consumer::AssetWriteLease& image,
                    std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
                an::Result<an::ui::ExecutionScope> scope =
                    admitRun(editSession, windowStop.has_value() ? *windowStop : stop, hook, observer, reads.get());
                if (!scope.hasValue())
                {
                    *failure = scope.status();
                    return scope.status();
                }
                std::optional<std::stop_callback<std::function<void()>>> forwardEditStop;
                if (windowStop.has_value())
                {
                    forwardEditStop.emplace(stop, [&scope]() { scope->requestStop(); });
                }
                an::Result<an::consumer::ImageAsset> made = operation(*scope, an::ui::ProcessSource::of(image));
                forwardEditStop.reset();
                const an::Status outcome =
                    made.hasValue() ? (scope->stopRequested() ? refusal(an::ErrorCode::Cancelled, "the run was stopped")
                                                              : an::Status::success())
                                    : made.status();
                scope->finish(outcome);
                if (outcome.failed())
                {
                    *failure = outcome;
                    return outcome;
                }
                return made;
            },
            [failure, words = request.words, inFlight = request.inFlight, progressGuard, done](bool shown) {
                if (progressGuard)
                {
                    progressGuard->closeAsync();
                }
                GeometryRunOutcome outcome;
                outcome.published = shown;
                if (!shown)
                {
                    // An edit the view did not show with no failure of the run's own was discarded by a change of the
                    // document, which is not a failure, or refused by the history, which says why itself.
                    outcome.status = failure->has_value()
                                         ? **failure
                                         : refusal(an::ErrorCode::Cancelled, "the view did not apply the change");
                    reportRunFailure(nullptr, words, outcome.status);
                }
                if (done)
                {
                    done(outcome);
                }
            },
            reads);
        return;
    }

    if (!view.tryRequestLockForProcessing())
    {
        view.showViewControlBusyWarning(request.words.title);
        if (done)
        {
            done(GeometryRunOutcome{.status = refusal(an::ErrorCode::Busy, "another run holds the view")});
        }
        return;
    }
    const std::optional<DocumentSequence> sequence = view.document().sequence();
    const std::optional<std::vector<QString>> paths =
        sequence.has_value() ? temporaryPaths(sequence->descriptor.format, 1u) : std::nullopt;
    if (!sequence.has_value() || !paths.has_value())
    {
        view.releaseLockForProcessing();
        const an::Status none =
            refusal(an::ErrorCode::NotInitialized,
                    sequence.has_value() ? "the application lends no temporary files" : "the view shows no sequence");
        reportRunFailure(nullptr, request.words, none);
        if (done)
        {
            done(GeometryRunOutcome{.status = none});
        }
        return;
    }
    ProgressDialogControl* progress = showProgress(request.words);
    const QPointer<ProgressDialogControl> progressGuard(progress);
    QPointer<ImageViewControl> guard(&view);
    const bool readDemosaiced = view.isSequenceAutodebayerEnabled();
    runThenLand(
        [session, sequence = sequenceToRead(*sequence, std::nullopt), paths = *paths, stop = progress->stopToken(),
         progressGuard, readDemosaiced, operation = request.operation, hook = request.admissionHook,
         reads = readsOf(request.reads)]() {
            return runOverFrames(
                *session, sequence, readDemosaiced, paths, stop, progressGuard,
                [&operation](const an::ui::ExecutionScope& scope,
                             const an::ui::ProcessSource& frame) -> an::Result<std::vector<an::consumer::ImageAsset>> {
                    an::Result<an::consumer::ImageAsset> made = operation(scope, frame);
                    if (!made.hasValue())
                    {
                        return made.status();
                    }
                    std::vector<an::consumer::ImageAsset> one;
                    one.push_back(std::move(*made));
                    return one;
                },
                hook, *reads);
        },
        [guard, words = request.words, stepName = request.stepName, stepDescription = request.stepDescription,
         paths = *paths, progressGuard, inFlight = request.inFlight, done](SequenceRun run) {
            if (guard)
            {
                guard->releaseLockForProcessing();
            }
            if (progressGuard)
            {
                progressGuard->closeAsync();
            }
            if (run.status.ok() && !guard)
            {
                run.status = refusal(an::ErrorCode::Cancelled, "the view closed before the sequence was shown");
            }
            GeometryRunOutcome outcome;
            if (run.status.ok())
            {
                DocumentNames names = guard->document().names();
                names.sourcePath = paths.front();
                an::Result<ImageDocument> document =
                    ImageDocument::create(std::move(run.shown.front()), std::move(names));
                if (!document.hasValue())
                {
                    run.status = document.status();
                }
                else
                {
                    document->setSequence(run.sequences.front());
                    guard->setSequenceDocument(std::move(*document), guard->isSequenceAutodebayerEnabled());
                    if (IViewHistory* history = guard->viewHistory())
                    {
                        (void)history->registerShownState(stepName, stepDescription);
                    }
                    outcome.published = true;
                    LOG_INFO("{}: sequence '{}'", stepName, guard->getUniqueName());
                }
            }
            if (run.status.failed())
            {
                removeSequences(paths, 0u);
                reportRunFailure(nullptr, words, run.status);
            }
            outcome.status = run.status;
            if (done)
            {
                done(outcome);
            }
        });
}

void runToNewViews(ImageViewControl& view, NewViewsRunRequest request, GeometryRunDone done)
{
    const an::consumer::EngineSession* session = lentEngineSession();
    const auto refuse = [&request, &done](an::Status status) {
        reportRunFailure(nullptr, request.words, status);
        if (done)
        {
            done(GeometryRunOutcome{.status = std::move(status)});
        }
    };
    if (session == nullptr)
    {
        refuse(refusal(an::ErrorCode::NotInitialized, "no engine session is lent"));
        return;
    }
    QPointer<ImageViewControl> guard(&view);
    if (!view.isSequence())
    {
        std::shared_ptr<const an::consumer::ImageAsset> asset = view.document().asset();
        if (asset == nullptr)
        {
            refuse(refusal(an::ErrorCode::NotInitialized, "the view shows no image"));
            return;
        }
        // The image is leased before the run is admitted: an edit of the view holds its writer while it waits for
        // admission, and a run admitted first would wait for the edit that waits for it.
        an::Result<an::consumer::AssetReadLease> lease = asset->tryRead();
        if (!lease.hasValue())
        {
            refuse(lease.status().code() == an::ErrorCode::Busy
                       ? refusal(an::ErrorCode::Busy, "the image is being changed; run it again once it is shown")
                       : lease.status());
            if (lease.status().code() == an::ErrorCode::Busy)
            {
                view.showViewControlBusyWarning(request.words.title);
            }
            return;
        }
        auto held = std::make_shared<an::consumer::AssetReadLease>(std::move(*lease));
        ProgressDialogControl* progress = request.progressWindow ? showProgress(request.words) : nullptr;
        const QPointer<ProgressDialogControl> progressGuard(progress);
        runThenLand(
            [session, held, operation = request.operation, hook = request.admissionHook,
             stop = progress != nullptr ? progress->stopToken() : std::stop_token{},
             observer = progress != nullptr ? progress->observer() : an::ui::ExecutionObserver{},
             reads = readsOf(request.reads)]() -> an::Result<std::vector<an::consumer::ImageAsset>> {
                // The images the operation reads beside the view's own are leased before the run is admitted, the
                // view's own read through the lease the run holds.
                reads->hold(held->share());
                const an::Status taken = reads->take(stop);
                const ReadsRelease letGo(*reads);
                if (taken.failed())
                {
                    held->release();
                    return taken;
                }
                an::Result<an::ui::ExecutionScope> scope = admitRun(*session, stop, hook, observer, reads.get());
                if (!scope.hasValue())
                {
                    held->release();
                    return scope.status();
                }
                an::Result<std::vector<an::consumer::ImageAsset>> made =
                    operation(*scope, an::ui::ProcessSource::of(*held));
                held->release();
                const an::Status outcome =
                    made.hasValue() ? (scope->stopRequested() ? refusal(an::ErrorCode::Cancelled, "the run was stopped")
                                                              : an::Status::success())
                                    : made.status();
                scope->finish(outcome);
                if (outcome.failed())
                {
                    return outcome;
                }
                return made;
            },
            [guard, outputs = request.imageOutputs, words = request.words, inFlight = request.inFlight, progressGuard,
             done](an::Result<std::vector<an::consumer::ImageAsset>> made) {
                if (progressGuard)
                {
                    progressGuard->closeAsync();
                }
                GeometryRunOutcome outcome;
                if (made.hasValue() && made->size() != outputs.size())
                {
                    made = refusal(an::ErrorCode::Internal, "the run made another number of images than views");
                }
                if (!made.hasValue())
                {
                    outcome.status = made.status();
                    reportRunFailure(nullptr, words, outcome.status);
                    if (done)
                    {
                        done(outcome);
                    }
                    return;
                }
                IViewFactory* factory = process_utils::currentViewFactory();
                for (std::size_t index = 0u; index < outputs.size(); ++index)
                {
                    an::Result<ImageDocument> document = ImageDocument::create(
                        std::move((*made)[index]),
                        DocumentNames{.displayName = QString::fromStdString(outputs[index].name)});
                    ImageViewControl* opened = nullptr;
                    if (document.hasValue() && factory != nullptr)
                    {
                        opened = factory->createNewImageViewControlFromImage(
                            nullptr, std::move(*document), nullptr, outputs[index].history, ViewControlMode::standard,
                            guard.data(), false, true);
                    }
                    if (opened == nullptr)
                    {
                        outcome.status = document.hasValue()
                                             ? refusal(an::ErrorCode::NotInitialized,
                                                       "no view could be opened for " + outputs[index].name)
                                             : document.status();
                        reportRunFailure(nullptr, words, outcome.status);
                        break;
                    }
                    outcome.published = true;
                    outcome.views.push_back(opened);
                    LOG_INFO("{} into new image '{}'", outputs[index].history, opened->getUniqueName());
                }
                if (done)
                {
                    done(outcome);
                }
            });
        return;
    }

    if (!view.tryRequestLockForProcessing())
    {
        view.showViewControlBusyWarning(request.words.title);
        if (done)
        {
            done(GeometryRunOutcome{.status = refusal(an::ErrorCode::Busy, "another run holds the view")});
        }
        return;
    }
    const std::optional<DocumentSequence> sequence = view.document().sequence();
    const std::optional<std::vector<QString>> paths =
        sequence.has_value() ? temporaryPaths(sequence->descriptor.format, request.sequenceOutputs.size())
                             : std::nullopt;
    if (!sequence.has_value() || !paths.has_value())
    {
        view.releaseLockForProcessing();
        refuse(refusal(an::ErrorCode::NotInitialized, sequence.has_value() ? "the application lends no temporary files"
                                                                           : "the view shows no sequence"));
        return;
    }
    ProgressDialogControl* progress = showProgress(request.words);
    const QPointer<ProgressDialogControl> progressGuard(progress);
    runThenLand(
        [session, sequence = sequenceToRead(*sequence, request.frames), paths = *paths, stop = progress->stopToken(),
         progressGuard, readDemosaiced = request.readDemosaiced, operation = request.operation,
         hook = request.admissionHook, reads = readsOf(request.reads)]() {
            return runOverFrames(*session, sequence, readDemosaiced, paths, stop, progressGuard, operation, hook,
                                 *reads);
        },
        [guard, outputs = request.sequenceOutputs, showDemosaiced = request.showDemosaiced, words = request.words,
         paths = *paths, progressGuard, inFlight = request.inFlight, done](SequenceRun run) {
            if (guard)
            {
                guard->releaseLockForProcessing();
            }
            if (progressGuard)
            {
                progressGuard->closeAsync();
            }
            GeometryRunOutcome outcome;
            if (run.status.ok() && run.sequences.size() != outputs.size())
            {
                run.status = refusal(an::ErrorCode::Internal, "the run made another number of sequences than views");
            }
            std::size_t shownFrom = 0u;
            if (run.status.ok())
            {
                IViewFactory* factory = process_utils::currentViewFactory();
                for (std::size_t index = 0u; index < outputs.size(); ++index)
                {
                    an::Result<ImageDocument> document =
                        ImageDocument::create(std::move(run.shown[index]),
                                              DocumentNames{.displayName = QString::fromStdString(outputs[index].name),
                                                            .sourcePath = paths[index]});
                    ImageViewControl* opened = nullptr;
                    if (document.hasValue())
                    {
                        document->setSequence(run.sequences[index]);
                        opened = factory != nullptr
                                     ? factory->createNewImageViewControlFromSequence(
                                           nullptr, std::move(*document), nullptr, showDemosaiced,
                                           outputs[index].history, ViewControlMode::standard, guard.data(), false, true)
                                     : nullptr;
                    }
                    if (opened == nullptr)
                    {
                        run.status = document.hasValue() ? refusal(an::ErrorCode::NotInitialized,
                                                                   "no view could be opened for " + outputs[index].name)
                                                         : document.status();
                        shownFrom = index;
                        break;
                    }
                    outcome.published = true;
                    outcome.views.push_back(opened);
                    LOG_INFO("{} into new sequence '{}'", outputs[index].history, opened->getUniqueName());
                }
            }
            if (run.status.failed())
            {
                removeSequences(paths, shownFrom);
                reportRunFailure(nullptr, words, run.status);
            }
            outcome.status = run.status;
            if (done)
            {
                done(outcome);
            }
        });
}

} // namespace an::ui::processes
