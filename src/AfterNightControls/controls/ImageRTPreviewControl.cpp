// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageRTPreviewControl.h"
#include "ImageViewControl.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewScrollArea.h"
#include "controls/ProcessPreviewLayout.h"
#include "controls/QSliderSpinBox.h"
#include "controls/settings/ProcessingLimits.h"
#include "app/AppContextHelpers.h"
#include "session/CommitTicket.h"
#include "session/DisplayProxy.h"
#include "session/ImageReads.h"
#include "session/IEngineSessionProvider.h"
#include "session/PreviewProxies.h"
#include "theme/AnDesignTokens.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramComputation.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ViewPresentation.h"
#include "widgets/waitingspinnerwidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <AfterNightPlatform/Logging.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QSplitter>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

static const QString PLOT_RESOLUTION_6BIT = "6-bit (64)";
static const QString PLOT_RESOLUTION_7BIT = "7-bit (128)";
static const QString PLOT_RESOLUTION_8BIT = "8-bit (256)";
static const QString PLOT_RESOLUTION_9BIT = "9-bit (512)";
static const QString PLOT_RESOLUTION_10BIT = "10-bit (1024)";
static const QString PLOT_RESOLUTION_12BIT = "12-bit (4096)";
static const QString PLOT_RESOLUTION_14BIT = "14-bit (16K)";
static const QString PLOT_RESOLUTION_16BIT = "16-bit (64K)";
static const QString PLOT_RESOLUTION_100_LEVELS = "100 levels";
static const QString PLOT_RESOLUTION_1000_LEVELS = "1000 levels";
static const QString PLOT_RESOLUTION_10000_LEVELS = "10000 levels";

namespace
{

using an::ui::kProcessMainPanelsSpacing;
using an::ui::kProcessMinPreviewPaneWidth;

an::ui::IIconProviderPtr currentIconProvider()
{
    auto* context = an::ui::appContext();
    return context ? context->getIconProvider() : nullptr;
}

/// The histogram resolution each plot label names.
[[nodiscard]] std::optional<an::ui::PreviewHistogramResolution> resolutionForLabel(const QString& label)
{
    using Resolution = an::ui::PreviewHistogramResolution;
    static const std::array<std::pair<const QString*, Resolution>, 11u> kResolutions{{
        {&PLOT_RESOLUTION_6BIT, Resolution::Bits6},
        {&PLOT_RESOLUTION_7BIT, Resolution::Bits7},
        {&PLOT_RESOLUTION_8BIT, Resolution::Bits8},
        {&PLOT_RESOLUTION_9BIT, Resolution::Bits9},
        {&PLOT_RESOLUTION_10BIT, Resolution::Bits10},
        {&PLOT_RESOLUTION_12BIT, Resolution::Bits12},
        {&PLOT_RESOLUTION_14BIT, Resolution::Bits14},
        {&PLOT_RESOLUTION_16BIT, Resolution::Bits16},
        {&PLOT_RESOLUTION_100_LEVELS, Resolution::Levels100},
        {&PLOT_RESOLUTION_1000_LEVELS, Resolution::Levels1000},
        {&PLOT_RESOLUTION_10000_LEVELS, Resolution::Levels10000},
    }};
    for (const auto& [text, resolution] : kResolutions)
    {
        if (*text == label)
        {
            return resolution;
        }
    }
    return std::nullopt;
}

/// A failure the framework decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// How long a drag held still waits before the settled frame it is owed is asked for.
constexpr std::chrono::milliseconds kSettleDelay{150};

/// How long a frame runs before the waiting spinner is shown.
constexpr std::chrono::milliseconds kSpinnerDelay{300};

/// The time between the starts of two frames: a second over the previews a second a settings store states. The store
/// answers on a thread of its own, so this is read off the GUI thread.
[[nodiscard]] std::chrono::nanoseconds framePeriodFrom(an::ui::IGlobalSettingsProvider& settings)
{
    const an::ui::SettingReader read = [&settings](const QString& key, const QString& fallback) {
        return settings.select(key, fallback);
    };
    return std::chrono::nanoseconds(std::chrono::seconds(1)) / an::ui::readPreviewFrameRate(read);
}

/// What a frame waiting to start and a new request for one collapse into: settled when either is.
[[nodiscard]] an::ui::PreviewFrameKind collapsedKind(std::optional<an::ui::PreviewFrameKind> waiting,
                                                     an::ui::PreviewFrameKind asked)
{
    return waiting == an::ui::PreviewFrameKind::Settled || asked == an::ui::PreviewFrameKind::Settled
               ? an::ui::PreviewFrameKind::Settled
               : an::ui::PreviewFrameKind::Interactive;
}

/// What a run's failure says to a person.
[[nodiscard]] QString describeFailure(const an::Status& status)
{
    return QString::fromStdString(fmt::format("An error occurred when running the process:\n{}", status.message()));
}

/// The bounds of the image a view shows, or an empty rectangle.
[[nodiscard]] QRect sourceImageBounds(const ImageViewControl* control)
{
    if (!control)
    {
        return {};
    }
    const std::optional<an::ImageDescriptor> described = control->imageDescriptor();
    if (!described.has_value())
    {
        return {};
    }
    return QRect(0, 0, static_cast<int>(described->extent().width()), static_cast<int>(described->extent().height()));
}

/// The priority a preview's frame is queued at on the thread pool, above every other task's, so a frame never waits
/// behind a thumbnail or a catalog read.
constexpr int kPreviewFramePriority = 100;

/// What a frame runs the process's body over.
enum class FrameSource : std::uint8_t
{
    /// The whole target, or its preview area, as every other run does.
    Target,

    /// The target reduced by the preview view's factor.
    Reduced,

    /// A part of the target at full scale: the part the preview view shows, or the region it presents.
    Region
};

/// The part of the target a view shows, aligned outward to the blocks of a reduction, so a reduction of the part is
/// made of the blocks a reduction of the whole target is.
[[nodiscard]] QRect alignedToBlocks(const an::ui::ViewRegion& shown, std::uint32_t factor)
{
    const std::uint64_t block = (std::max)(1u, factor);
    const std::uint64_t left = shown.x / block * block;
    const std::uint64_t top = shown.y / block * block;
    const std::uint64_t right = (std::uint64_t{shown.x} + shown.width + block - 1u) / block * block;
    const std::uint64_t bottom = (std::uint64_t{shown.y} + shown.height + block - 1u) / block * block;
    return QRect(static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left),
                 static_cast<int>(bottom - top));
}

/// How a frame makes what the dialog's histogram shows.
enum class FrameHistogram : std::uint8_t
{
    /// It makes nothing: the dialog shows no histogram.
    None,

    /// The count of the frame's whole result.
    Counted,

    /// The target's codes remapped through what the body makes of every code.
    Remapped,

    /// The count of the body's result over the target sampled one pixel per block.
    Sampled
};

/// Whether a scope runs over the stored channels as they are, each mapped alone by a body that maps each channel alone.
[[nodiscard]] bool isStoredChannelScope(an::ChannelScope scope) noexcept
{
    return scope == an::ChannelScope::Whole || scope == an::ChannelScope::Red || scope == an::ChannelScope::Green ||
           scope == an::ChannelScope::Blue;
}

/// The time from a moment to now.
[[nodiscard]] std::chrono::nanoseconds elapsedSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
}

/// Whether a presentation measures what it presents - a stretch's statistics or a colour balance - and so needs the
/// analysis it is stretched through.
[[nodiscard]] bool presentationMeasures(const an::DisplayPresentationRequest& request) noexcept
{
    return request.stretch == an::PresentationStretch::Automatic || request.neutralize || request.calibrate;
}

/// Whether every sample of an image read under a lease is valid: a rule that calls every one valid, and no mask.
[[nodiscard]] bool everySampleValid(const an::consumer::AssetReadLease& read)
{
    if (read.validity() != nullptr)
    {
        return false;
    }
    return std::visit(
        [](const auto& typed) { return typed.metadata().semantics().validity().rule() == an::ValidityRule::AllValid; },
        read.image());
}

/// Whether a preview area's patch is the target's kind of image - the target's description at the patch's size - with
/// every sample valid: then the composite is the target outside the area and the patch within it.
[[nodiscard]] bool patchIsTargetsKind(const an::consumer::AssetReadLease& patch, const an::ImageDescriptor& target)
{
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& typed) { return typed.descriptor(); }, patch.image());
    if (!described.has_value() || !everySampleValid(patch))
    {
        return false;
    }
    an::ImageDescriptorRequest atPatchSize = target.toRequest();
    atPatchSize.extent = described->extent();
    const std::optional<an::ImageDescriptor> restated = an::ImageDescriptor::create(atPatchSize);
    return restated.has_value() && *restated == *described;
}

/// Whether an image holds one plane per channel.
[[nodiscard]] bool isPlanar(const an::consumer::AnyImage& image)
{
    return std::visit(
        [](const auto& typed) {
            const auto view = typed.view();
            return view.has_value() && view->layout().samplesPerPixel() == 1u;
        },
        image);
}

/// A rectangle of the target as the proxy cache files a crop under it.
[[nodiscard]] an::ui::PreviewPart partOf(const QRect& rect) noexcept
{
    return an::ui::PreviewPart{.x = static_cast<std::uint32_t>((std::max)(0, rect.x())),
                               .y = static_cast<std::uint32_t>((std::max)(0, rect.y())),
                               .width = static_cast<std::uint32_t>((std::max)(0, rect.width())),
                               .height = static_cast<std::uint32_t>((std::max)(0, rect.height()))};
}

/// The widest reach around a region a process may state, wider than any image, so the arithmetic on the rectangle
/// stays in range whatever a process answers.
constexpr std::uint32_t kMaxRegionMargin = 1u << 20u;

/// The bounds of an asset's image, or an empty rectangle.
[[nodiscard]] QRect assetBounds(const std::shared_ptr<const an::consumer::ImageAsset>& asset)
{
    const std::optional<an::ImageDescriptor> described = asset ? asset->descriptor() : std::nullopt;
    if (!described.has_value())
    {
        return {};
    }
    return QRect(0, 0, static_cast<int>(described->extent().width()), static_cast<int>(described->extent().height()));
}

/// What a run's worker brings back to the GUI thread.
struct RunResult
{
    /// What the run came to.
    an::Status status = an::Status::success();

    /// The image to publish: a preview's result or composite, a new image, a sequence's shown frame.
    std::optional<an::consumer::ImageAsset> asset;

    /// An interactive frame's result over a reduction or a region of the target, which only its presentation and its
    /// histogram show.
    std::optional<an::consumer::ImageAsset> frameImage;

    /// What the frame presented for the preview view, and the display proxy of its result it presented from.
    std::optional<an::ui::ViewPresentationOutcome> presented;
    std::shared_ptr<const an::ui::DisplayProxy> resultProxy;

    /// What the dialog's histogram shows of the frame, and how it was made.
    std::shared_ptr<const an::ui::HistogramDisplayData> histogram;

    /// The reductions, samples and code counts the frame made, for the GUI thread to keep for their revisions.
    std::shared_ptr<const an::consumer::ImageAsset> newReduction;
    std::shared_ptr<const an::consumer::ImageAsset> newMaskReduction;
    std::shared_ptr<const an::consumer::ImageAsset> newSample;
    std::shared_ptr<const an::consumer::ImageAsset> newMaskSample;
    std::shared_ptr<const an::ui::CodeHistogram> newCodes;

    /// The conversions the frame made of what it read and those it read of what was kept, and how many of each.
    std::vector<an::ui::ProcessConversion> conversionsMade;
    std::vector<an::ui::ProcessConversion> conversionsReused;
    std::uint64_t scopesPrepared = 0u;
    std::uint64_t scopesReused = 0u;
    std::uint64_t planarMade = 0u;
    std::uint64_t planarReused = 0u;

    /// The crops of the target and of its mask the frame made, for the GUI thread to keep, and how many it read kept.
    std::shared_ptr<const an::consumer::ImageAsset> newCrop;
    std::shared_ptr<const an::consumer::ImageAsset> newMaskCrop;
    std::uint64_t cropsReused = 0u;

    /// A frame over the preview area shown as its patch over what the view paints: the patch's presentation, covering
    /// the area in the target's pixels.
    std::optional<an::ui::ViewPresentationOutcome> patchPresented;

    /// The counts of the target and of its part under the area that a patch frame's histogram was derived from, made
    /// by this frame for the GUI thread to keep; and whether the histogram was so derived.
    std::shared_ptr<const an::ui::HistogramDisplayData> newTargetCounts;
    std::shared_ptr<const an::ui::HistogramDisplayData> newCropCounts;
    bool histogramDerived = false;

    /// How long the frame waited to be admitted, and how long it then worked.
    std::chrono::nanoseconds admissionWait{0};
    std::chrono::nanoseconds work{0};

    /// The preview area's composite base the run built, for the GUI thread to keep for the target's revision.
    std::shared_ptr<const an::consumer::ImageAsset> newBase;

    /// The analysis the preview is stretched through, when the process stretches it from the target.
    std::optional<an::DisplayPresentationAnalysis> analysis;

    /// Whether `analysis` was measured by this run rather than kept from an earlier one.
    bool analysisMeasured = false;

    /// The images a frame's body made beside its result, blended as the result was, for the GUI thread to keep.
    std::vector<an::ui::ProcessAlternative> alternatives;

    /// The sequence a sequence run wrote, once it is whole at its path.
    std::optional<an::ui::DocumentSequence> sequence;
};

/// Posts the overall progress of a sequence run to its dialog, from any thread; nothing happens once the dialog is
/// gone.
void postSequenceProgress(const QPointer<ProgressDialogControl>& dialog, int value, const QString& text)
{
    QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(
        application,
        [dialog, value, text]() {
            if (dialog)
            {
                dialog->setSeqValue(value);
                dialog->setSeqText(text);
            }
        },
        Qt::QueuedConnection);
}

/// Calls back each time the widget it watches is shown or resized.
class GeometryWatch final : public QObject
{
public:
    explicit GeometryWatch(std::function<void()> changed) : m_changed(std::move(changed)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
        {
            m_changed();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_changed;
};

} // namespace

/// Everything a run read on the GUI thread when it was issued: what the worker reads, and nothing it may not.
struct ImageRTPreviewControl::RunSnapshot
{
    /// Why the run runs, on what channel, and which kind of frame a preview is.
    an::ui::ProcessRun run;

    /// The most threads the run asks admission for: the session's preview reservation for an interactive frame, so it
    /// never borrows past it; zero, as many as its purpose may have, for every other run.
    std::uint32_t maxConcurrency = 0u;

    /// The process's parameters.
    ProcessExecutionParamsPtr params;

    /// How far the result is blended back toward the source.
    double modulation = 1.0;

    /// The target's image when the run was issued.
    std::shared_ptr<const an::consumer::ImageAsset> target;

    /// The mask the target applies, or null.
    std::shared_ptr<const an::consumer::ImageAsset> mask;

    /// Whether the mask weighs the source rather than the result.
    bool invertMask = false;

    /// The preview area, in the target's pixels; empty for the whole image.
    QRect area;

    /// The history step an Apply records.
    an::ui::ProcessStep step;

    /// The image whose statistics stretch the preview, or null for none.
    std::shared_ptr<const an::consumer::ImageAsset> statsSource;

    /// What the preview view presents through, for the stretch's analysis.
    std::optional<an::ui::ViewPresentationSettings> statsSettings;

    /// The analysis kept for the stats source's revision and those settings, if any.
    std::optional<an::DisplayPresentationAnalysis> keptAnalysis;

    /// The preview area's composite base kept for the target's revision, if any.
    std::shared_ptr<const an::consumer::ImageAsset> keptBase;

    /// What the preview view would paint a frame's presentation with, when it paints one; a frame without it hands the
    /// view its result to present.
    std::optional<an::ui::ViewPresentationSettings> presentation;

    /// The key a preview's result is kept under, for its parameters; empty keeps nothing.
    std::string resultKey;

    /// What the frame runs the body over, and the reduction's factor; the reductions of the target and of its mask kept
    /// for their revisions at that factor, if any.
    FrameSource frameSource = FrameSource::Target;
    std::uint32_t frameFactor = 1u;
    std::shared_ptr<const an::consumer::ImageAsset> keptReduction;
    std::shared_ptr<const an::consumer::ImageAsset> keptMaskReduction;

    /// How the frame makes the dialog's histogram, at what resolution; for a sampled one, the factor and the samples of
    /// the target and its mask kept for their revisions; for a remapped one, the target's code counts kept.
    FrameHistogram histogram = FrameHistogram::None;
    an::ui::HistogramResolution histogramResolution = an::ui::HistogramResolution::Bits8;
    std::uint32_t sampleFactor = 1u;
    std::shared_ptr<const an::consumer::ImageAsset> keptSample;
    std::shared_ptr<const an::consumer::ImageAsset> keptMaskSample;
    std::shared_ptr<const an::ui::CodeHistogram> keptCodes;

    /// The conversions earlier frames made of what frames read, which this frame may read.
    std::vector<an::ui::ProcessConversion> conversions;

    /// The part of the target a frame over a part of it runs over - the preview area, the part of the target the view
    /// shows, or the region it presents - and the crops of the target and of its mask under it kept for their
    /// revisions, if any.
    QRect cropPart;

    /// The part of the target the body of such a frame runs over: the crop part, grown within the target by the
    /// process's reach around it, so its edge is answered as it is over the whole image; the crop part itself under the
    /// preview area.
    QRect bodyPart;

    /// Whether a frame over a part of the target the view shows is shown as a layer over what the view paints - its
    /// crop part the part shown, aligned to the view's reduction - rather than as the view's whole presentation of the
    /// region it presents.
    bool regionLayer = false;
    std::shared_ptr<const an::consumer::ImageAsset> keptCrop;
    std::shared_ptr<const an::consumer::ImageAsset> keptMaskCrop;

    /// Whether an interactive frame over the preview area may be shown as its patch over what the view paints now,
    /// which is the view's presentation of its document under the frame's settings; and the counts its histogram is
    /// derived from, kept for the target's revision and for the crop's, if any.
    bool patchFrame = false;
    std::shared_ptr<const an::ui::HistogramDisplayData> keptTargetCounts;
    std::shared_ptr<const an::ui::HistogramDisplayData> keptCropCounts;

    /// The run's commit ticket; empty for an Apply, which is the view's edit.
    an::ui::CommitTicket ticket;

    /// Where the run's progress goes.
    an::ui::ExecutionObserver observer;

    /// The progress dialog's Cancel.
    std::stop_token cancel;

    /// The progress dialog, for a sequence run's overall progress.
    QPointer<ProgressDialogControl> progressDialog;

    /// Every image the run reads - the target, its mask, the image a stretch's statistics read and the process's own -
    /// leased before the run asks to be admitted; an Apply in place reads its target through its edit's writer.
    std::shared_ptr<an::ui::ImageReads> reads = std::make_shared<an::ui::ImageReads>();

    /// The sequence a sequence run reads, and whether it demosaics its frames.
    std::optional<an::ui::DocumentSequence> sequence;
    bool autoDebayer = false;

    /// The file a sequence run publishes at.
    QString outputPath;
};

namespace
{

/// The completion of a run whose work threw outside its body's own guard - a reduction, a histogram or a conversion
/// out of memory, a process's result hook: the run ends with a failure the window says, so the window's preview, its
/// controls and its target are not left busy for the window's life.
[[nodiscard]] RunResult resultOfWorkThatThrew(const char* what)
{
    RunResult result;
    result.status = refusal(an::ErrorCode::Internal, what);
    return result;
}

/// The body a run calls and the hook it hands the finished result to, both the process's.
using RunBody = std::function<an::Status(const an::ui::ProcessSource&, an::ui::ProcessDestination&,
                                         const an::ui::ProcessRun&, an::ui::ExecutionScope&)>;
using ResultHook = std::function<void(const an::ui::ProcessSource&, const an::ui::ProcessRun&)>;

/// The framework's work around a body over one image: the destination - a copy of the source, or unwritten for a body
/// that writes every sample - the body, the modulation and the mask, and the finished-result hook. The mask's revision
/// files its conversion among the source's conversions, when the source carries a run's. The images the body made
/// beside its result are blended as the result is and handed to `alternatives`, or let go when it is null.
[[nodiscard]] an::Result<an::consumer::ImageAsset>
processOneImage(const an::consumer::EngineSession& session, an::ui::ExecutionScope& scope,
                const an::ui::ProcessSource& source, const an::consumer::AnyImage* mask, const an::ui::ProcessRun& run,
                double modulation, bool invertMask, const RunBody& body, const ResultHook& ready,
                const an::consumer::AssetRevision& maskRevision = {},
                std::vector<an::ui::ProcessAlternative>* alternatives = nullptr)
{
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::forBody(session, scope.engine(), scope.execution(), source, run.coverage);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    an::Status ran = an::Status::success();
    try
    {
        ran = body(source, *destination, run, scope);
    }
    catch (const std::exception& error)
    {
        ran = refusal(an::ErrorCode::Internal, error.what());
    }
    catch (...)
    {
        ran = refusal(an::ErrorCode::Internal, "Unknown error occurred when running the process");
    }
    if (ran.failed())
    {
        return ran;
    }
    if (scope.stopRequested())
    {
        return refusal(an::ErrorCode::Cancelled, "the run was stopped");
    }
    const an::ui::ProcessBlendRequest blend{
        .modulation = modulation, .mask = mask, .maskRevision = maskRevision, .invertMask = invertMask};
    const an::Status blended =
        an::ui::blendProcessResult(scope.engine(), scope.execution(), source, *destination, blend);
    if (blended.failed())
    {
        return blended;
    }
    if (alternatives != nullptr)
    {
        // An image the blend refuses is not kept: a frame for its parameters makes it when they are asked for.
        for (an::ui::ProcessAlternative& alternative : destination->takeAlternatives())
        {
            if (an::ui::blendProcessAlternative(scope.engine(), scope.execution(), source, alternative.image, blend)
                    .ok())
            {
                alternatives->push_back(std::move(alternative));
            }
        }
    }
    if (ready)
    {
        ready(an::ui::ProcessSource::of(destination->asset()), run);
    }
    return destination->release();
}

/// A read lease on a mask, unless the mask is the image already leased, whose lease is then read through.
struct MaskRead
{
    an::consumer::AssetReadLease lease;
    const an::consumer::AnyImage* image = nullptr;
    const an::ValidityMask* validity = nullptr;
};

[[nodiscard]] an::Result<MaskRead> readMask(const std::shared_ptr<const an::consumer::ImageAsset>& mask,
                                            const an::ui::ImageReads* reads, std::uint64_t leasedIdentity,
                                            const an::consumer::AnyImage& leasedImage,
                                            const an::ValidityMask* leasedValidity, const std::stop_token& stop)
{
    MaskRead read;
    if (!mask)
    {
        return read;
    }
    if (mask->revision().assetIdentity == leasedIdentity)
    {
        // A second lease on the same asset would queue behind any writer waiting for it, while this run holds the
        // first: the mask is read through the lease already held.
        read.image = &leasedImage;
        read.validity = leasedValidity;
        return read;
    }
    // Another view's mask the run leased before it was admitted is read through a share of that lease.
    if (const an::consumer::AssetReadLease* held = reads != nullptr ? reads->find(mask->identity()) : nullptr;
        held != nullptr)
    {
        read.lease = held->share();
        read.image = &read.lease.image();
        read.validity = read.lease.validity();
        return read;
    }
    an::Result<an::consumer::AssetReadLease> lease = mask->read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    read.lease = std::move(*lease);
    read.image = &read.lease.image();
    read.validity = read.lease.validity();
    return read;
}

/// The revision of the asset a mask was read from: its own lease's, or the leased image's when it is that image.
[[nodiscard]] an::consumer::AssetRevision maskRevisionOf(const MaskRead& mask,
                                                         const an::consumer::AssetReadLease& leased) noexcept
{
    if (mask.image == nullptr)
    {
        return {};
    }
    return mask.lease.valid() ? mask.lease.revision() : leased.revision();
}

/// A share of the lease a run holds on an image it named; refused when it holds none.
[[nodiscard]] an::Result<an::consumer::AssetReadLease> heldShare(const an::ui::ImageReads& reads,
                                                                 const an::consumer::ImageAsset& image)
{
    const an::consumer::AssetReadLease* held = reads.find(image.identity());
    if (held == nullptr)
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "the run holds no lease on an image it reads");
    }
    return held->share();
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

/// A stop source stopped by either of two tokens, for as long as it lives.
class LinkedStop
{
public:
    LinkedStop(const std::stop_token& first, const std::stop_token& second)
        : m_first(first, Stopper{&m_source}), m_second(second, Stopper{&m_source})
    {
    }

    [[nodiscard]] std::stop_token token() const noexcept
    {
        return m_source.get_token();
    }

private:
    struct Stopper
    {
        std::stop_source* source;
        void operator()() const noexcept
        {
            source->request_stop();
        }
    };

    std::stop_source m_source;
    std::stop_callback<Stopper> m_first;
    std::stop_callback<Stopper> m_second;
};

} // namespace

struct ImageRTPreviewControl::Impl
{
    bool m_ShowWaitingSpinner = true;

    std::atomic_bool m_SuspendBackgroundUpdates = false;
    std::atomic_bool m_IgnoreLastImageUpdatedEvent = false;

    ImageViewControl* m_rt_preview_view_control = nullptr;

    bool m_rt_preview_use_sub_area = false;
    bool m_PreviewAreaSelectionEnabled = true;
    QRect m_rt_preview_sub_area = QRect(0, 0, 0, 0);

    QSize m_InitiaSubAreaSize;
    QString m_SubAreaText;

    QSize m_DefaultSizePreview = QSize(1000, 700);
    QSize m_DefaultSizeNoPreview = QSize(400, 200);

    QLayout* m_ImagePreviewLayout = nullptr;
    QScrollArea* m_HistogramPreviewScrollArea = nullptr;
    QBoxLayout* m_MainHorizontalLayout = nullptr;

    QComboBox* m_PlotResolution = nullptr;

    QAbstractButton* m_ZoomIn = nullptr;
    QAbstractButton* m_ZoomOut = nullptr;
    QAbstractButton* m_ZoomReset = nullptr;
    QAbstractButton* m_GridButton = nullptr;
    QAbstractButton* m_LocalChannelNormalization = nullptr;
    QAbstractButton* m_ShowClippingInfo = nullptr;
    QAbstractButton* m_ShowHistogramAreas = nullptr;
    QAbstractButton* m_ShowHistogram = nullptr;

    std::unique_ptr<ImageCropController> m_CropController;

    bool m_SupportMasking = true;
    bool m_FixedLayout = true;
    bool m_CreatedViewControlsAreNeighbour = false;

    std::unique_ptr<WaitingSpinnerWidget> m_ImagePreviewSpinner;

    bool m_FitToWindowRequested = false;
    // The size of the target's image the preview view was fitted to as it opened, until its first result lands: that
    // result is fitted again only when it is of another size, so the view neither waits zoomed for it nor jumps.
    std::optional<QSize> m_FittedBeforeFirstResult;
    // The zoom that fit gave: the view is fitted again as its viewport is shown or resized until the first result
    // lands, unless the person zoomed since.
    double m_FittedBeforeFirstResultFactor = 0.0;
    // Watches the preview view's viewport while the view shows the target's image, and whether a fit is queued.
    std::unique_ptr<QObject> m_PreviewGeometryWatch;
    bool m_PreviewFitQueued = false;

    bool m_UseHistogramPreviewWidget = false;
    std::unique_ptr<an::ui::IPreviewHistogram> m_HistogramPreview;

    bool m_SupportExecutionAutostretch = false;
    bool m_StartupPreviewPending = false;

    // ---- The preview's scheduling, on the GUI thread.

    /// Issues the previews; issuing one supersedes the one before.
    an::ui::RequestTracker m_Previews;

    /// Whether a preview is on its way.
    bool m_PreviewRunning = false;

    /// Whether the preview on its way was asked for as a settled frame - a drag's end, a change made without a held
    /// slider, the framework's own - which a drag that starts again may stop.
    bool m_RunningFrameStoppable = false;

    /// The kind of the frame asked for and not yet started - while one runs or the frame period has not passed - into
    /// which every further request collapses; it starts, once, with the parameters the window holds then.
    std::optional<an::ui::PreviewFrameKind> m_WantedFrame;

    /// Whether the pacing timer is armed to start the frame wanted.
    bool m_PreviewTimerArmed = false;

    /// When the last frame started.
    QElapsedTimer m_LastPreviewStart;

    /// The time between frame starts, taken when a chain of frames begins - a request that finds no frame running,
    /// wanted or paced - from the last read of the settings that landed, and kept for the frames that follow it.
    std::chrono::nanoseconds m_FramePeriod{std::chrono::nanoseconds(std::chrono::seconds(1)) / 30};

    /// What the settings store last answered for the period, read off the GUI thread; the next chain takes it.
    std::chrono::nanoseconds m_NextFramePeriod{std::chrono::nanoseconds(std::chrono::seconds(1)) / 30};

    /// Whether a read of the frame rate is on its way.
    bool m_FramePeriodRefreshing = false;

    /// Whether the window is being destroyed: nothing is read from the application then, whose context may be going.
    bool m_Destroying = false;

    /// The sliders the window watches that a person holds now.
    std::vector<const QObject*> m_ControlsHeld;

    /// What the watch of those sliders is made on, so that it goes with this state: a slider may be one of the window's
    /// children, which the window's base deletes once the window's own destructor has let this state go, and its going
    /// must reach nothing of it then.
    QObject m_ControlWatch;

    /// Whether an interactive frame was asked for since the last settled one started, so a settled one is owed when the
    /// drag ends.
    bool m_SettledFrameOwed = false;

    /// Asks for the settled frame a drag held still for the settle delay is owed.
    QTimer m_SettleTimer;

    /// While above zero a reset is changing the parameters: requests are held back, and one is asked for when it ends.
    int m_PreviewHolds = 0;

    // ---- What the window shows while frames run.

    /// Shows the waiting spinner once a frame has run for the spinner's delay.
    QTimer m_SpinnerDelay;

    /// Whether the waiting spinner is shown.
    bool m_SpinnerShown = false;

    /// The cause of the preview failure the banner reports, and the banner's text for it; empty while none is.
    std::string m_ReportedFailure;
    QString m_ReportedFailureText;

    /// The dialog's claim on the session's preview reservation while its preview is shown.
    an::consumer::PreviewReservation m_PreviewReservation;

    // ---- What earlier previews left to reuse, on the GUI thread.

    /// The preview area's composite base and the target revision it was built from.
    an::ui::DocumentRevision m_CompositeBaseSource;
    std::shared_ptr<const an::consumer::ImageAsset> m_CompositeBase;

    /// The analysis the preview was stretched through, and what it was measured for.
    an::consumer::AssetRevision m_AnalysisSource;
    std::string m_AnalysisPolicy;
    std::optional<an::DisplayPresentationAnalysis> m_Analysis;

    /// The reductions, samples and crops of the target and its mask that frames read, each filed by the revision it was
    /// made from, so a change of either - inside the dialog or outside it - never finds a stale one.
    an::ui::PreviewProxyCache m_FrameProxies{12u};

    /// Every code's count in each channel of the target, and the revision they were counted on.
    an::consumer::AssetRevision m_CodesSource;
    std::shared_ptr<const an::ui::CodeHistogram> m_Codes;

    /// The conversions frames made of what they read - forward halves of a scope, masks and sources laid out a plane
    /// per channel - each filed by the revision it was made from.
    an::ui::ProcessConversionCache m_Conversions{8u};

    /// The counts of the target, and of the crop under the preview area, that patch frames derive their histograms
    /// from, each with the revision and the resolution it was counted at.
    an::consumer::AssetRevision m_TargetCountsSource;
    an::ui::HistogramResolution m_TargetCountsResolution = an::ui::HistogramResolution::Bits8;
    std::shared_ptr<const an::ui::HistogramDisplayData> m_TargetCounts;
    an::consumer::AssetRevision m_CropCountsSource;
    an::ui::HistogramResolution m_CropCountsResolution = an::ui::HistogramResolution::Bits8;
    std::shared_ptr<const an::ui::HistogramDisplayData> m_CropCounts;

    /// The results the last frame over the whole target made, under the keys of the parameters that make them - its
    /// own, and those its body made beside it - with what they were made from and how they were blended; a frame whose
    /// parameters carry one of those keys over the same images and blend shows it instead of running.
    struct KeptPreviews
    {
        an::consumer::AssetRevision target;
        an::consumer::AssetRevision mask;
        bool masked = false;
        bool invertMask = false;
        double modulation = 1.0;
        an::ChannelScope channel = an::ChannelScope::Whole;
        std::vector<std::pair<std::string, std::shared_ptr<const an::consumer::ImageAsset>>> results;
    };
    std::optional<KeptPreviews> m_KeptPreviews;

    // ---- The runs that publish elsewhere than the preview: a new image, a snapshot, a sequence.

    an::ui::RequestTracker m_Runs;

    ProcessRunTelemetry m_Telemetry;

    /// Last splitter proportions recorded before the preview was hidden,
    /// so they can be restored when the preview is re-enabled.
    QList<int> m_LastPreviewSplitterSizes;

    /// Last window geometry recorded before the preview was hidden.
    QSize m_LastPreviewWindowSize;
    bool m_HasLastPreviewSplitterSizes = false;
    bool m_HasLastPreviewWindowSize = false;

    /// Window constraints saved at the moment the preview was disabled.
    /// They are restored when the preview is re-enabled.
    QSize m_SavedWindowMaxSize;

    /// The layout's sizeConstraint that was active before the preview was
    /// hidden (typically QLayout::SetMinimumSize).  While the preview is
    /// off, the constraint is dropped to SetDefaultConstraint so that the
    /// window can shrink to the controls-only width.  Restored when the
    /// preview is re-enabled.
    QLayout::SizeConstraint m_SavedLayoutConstraint = QLayout::SetMinimumSize;

    /// True while the preview is disabled and we are holding saved window
    /// constraints (max size, layout constraint) to restore later.
    bool m_PreviewOffWindowConstraintsSaved = false;
};

ImageRTPreviewControl::ImageRTPreviewControl(ImageViewControl* control, const std::string& name)
    : ImageProcessControl(control, name), m_impl(std::make_unique<Impl>()),
      m_ShowWaitingSpinner(m_impl->m_ShowWaitingSpinner),
      m_SuspendBackgroundUpdates(m_impl->m_SuspendBackgroundUpdates),
      m_IgnoreLastImageUpdatedEvent(m_impl->m_IgnoreLastImageUpdatedEvent),
      m_rt_preview_view_control(m_impl->m_rt_preview_view_control),
      m_rt_preview_use_sub_area(m_impl->m_rt_preview_use_sub_area),
      m_rt_preview_sub_area(m_impl->m_rt_preview_sub_area), m_InitiaSubAreaSize(m_impl->m_InitiaSubAreaSize),
      m_SubAreaText(m_impl->m_SubAreaText), m_DefaultSizePreview(m_impl->m_DefaultSizePreview),
      m_DefaultSizeNoPreview(m_impl->m_DefaultSizeNoPreview), m_ImagePreviewLayout(m_impl->m_ImagePreviewLayout),
      m_HistogramPreviewScrollArea(m_impl->m_HistogramPreviewScrollArea),
      m_MainHorizontalLayout(m_impl->m_MainHorizontalLayout), m_PlotResolution(m_impl->m_PlotResolution),
      m_ZoomIn(m_impl->m_ZoomIn), m_ZoomOut(m_impl->m_ZoomOut), m_ZoomReset(m_impl->m_ZoomReset),
      m_GridButton(m_impl->m_GridButton), m_LocalChannelNormalization(m_impl->m_LocalChannelNormalization),
      m_ShowClippingInfo(m_impl->m_ShowClippingInfo), m_ShowHistogramAreas(m_impl->m_ShowHistogramAreas),
      m_ShowHistogram(m_impl->m_ShowHistogram), m_CropController(m_impl->m_CropController),
      m_SupportMasking(m_impl->m_SupportMasking), m_FixedLayout(m_impl->m_FixedLayout),
      m_CreatedViewControlsAreNeighbour(m_impl->m_CreatedViewControlsAreNeighbour),
      m_ImagePreviewSpinner(m_impl->m_ImagePreviewSpinner), m_FitToWindowRequested(m_impl->m_FitToWindowRequested),
      m_UseHistogramPreviewWidget(m_impl->m_UseHistogramPreviewWidget), m_HistogramPreview(m_impl->m_HistogramPreview),
      m_SupportExecutionAutostretch(m_impl->m_SupportExecutionAutostretch)
{
    setProperty("anForceBottomActionBar", true);

    // A drag held still is owed its settled frame once the settle delay passes with no change.
    m_impl->m_SettleTimer.setSingleShot(true);
    m_impl->m_SettleTimer.setInterval(kSettleDelay);
    QObject::connect(&m_impl->m_SettleTimer, &QTimer::timeout, this, [this]() {
        if (m_impl->m_SettledFrameOwed)
        {
            requestPreview(an::ui::PreviewFrameKind::Settled);
        }
    });

    // The waiting spinner is shown only for a frame that is still running once its delay has passed.
    m_impl->m_SpinnerDelay.setSingleShot(true);
    m_impl->m_SpinnerDelay.setInterval(kSpinnerDelay);
    QObject::connect(&m_impl->m_SpinnerDelay, &QTimer::timeout, this, [this]() {
        if (m_impl->m_PreviewRunning && m_ShowWaitingSpinner && !m_impl->m_SpinnerShown)
        {
            m_impl->m_SpinnerShown = true;
            ++m_impl->m_Telemetry.spinnersShown;
            controlSpinner(0);
        }
    });
}

void ImageRTPreviewControl::initializeStandardProcess(QLayout* mainLayout, QLayout* previewLayout,
                                                      QCheckBox* previewCheckbox, QDialogButtonBox* buttonBox,
                                                      QComboBox* targetSelector, QComboBox* channelSelector,
                                                      bool supportPerChannel, bool showWaitingSpinner)
{
    initializeStandardProcess(mainLayout, previewLayout, previewCheckbox, buttonBox, targetSelector, channelSelector,
                              {.supportPerChannel = supportPerChannel, .showWaitingSpinner = showWaitingSpinner});
}

void ImageRTPreviewControl::initializeStandardProcess(QLayout* mainLayout, QLayout* previewLayout,
                                                      QCheckBox* previewCheckbox, QDialogButtonBox* buttonBox,
                                                      QComboBox* targetSelector, QComboBox* channelSelector,
                                                      const ProcessInitOptions& options)
{
    setAttribute(Qt::WA_DeleteOnClose);

    m_MainHorizontalLayout = dynamic_cast<QBoxLayout*>(mainLayout);
    m_ImagePreviewLayout = previewLayout;

    if (m_MainHorizontalLayout)
    {
        // Preserve the UiBuilder's size constraint (typically SetMinimumSize)
        // so Qt continuously enforces the layout's computed minimum as the
        // window floor while the preview is visible.  Only override if the
        // UiBuilder left the default.
        //
        // NOTE: When the preview is subsequently disabled, the constraint is
        // temporarily dropped to SetDefaultConstraint (see the unchecked
        // branch of on_rt_preview_clicked) so that setFixedSize() can
        // shrink the window past the stale minimumSizeHint.  It is
        // restored when the preview is re-enabled.
        if (m_MainHorizontalLayout->sizeConstraint() == QLayout::SetDefaultConstraint)
        {
            m_MainHorizontalLayout->setSizeConstraint(QLayout::SetMinimumSize);
        }
        m_MainHorizontalLayout->setSpacing(kProcessMainPanelsSpacing);
    }

    if (QSplitter* splitter = an::ui::findMainProcessSplitter(m_MainHorizontalLayout))
    {
        if (splitter->count() > 0)
        {
            an::ui::alignSliderRowLabels(splitter->widget(0));
        }
    }

    // Zero the preview layout margins and spacing so the preview area
    // top-aligns with the controls column.  Most UiBuilders leave the
    // preview layout at Qt's platform defaults, which pushes the preview
    // down relative to the controls.
    if (m_ImagePreviewLayout)
    {
        const bool keepPreviewLayoutMargins = m_ImagePreviewLayout->property("anKeepPreviewLayoutMargins").toBool();
        if (!keepPreviewLayoutMargins)
        {
            m_ImagePreviewLayout->setContentsMargins(0, 0, 0, 0);
            m_ImagePreviewLayout->setSpacing(0);
        }
    }

    if (previewCheckbox)
    {
        QObject::connect(previewCheckbox, &QCheckBox::checkStateChanged, this,
                         &ImageRTPreviewControl::on_rt_preview_clicked);
    }

    if (buttonBox)
    {
        QObject::connect(buttonBox, &QDialogButtonBox::clicked, this,
                         &ImageRTPreviewControl::on_rt_dialog_box_button_clicked);

        if (QAbstractButton* applyButton = buttonBox->button(QDialogButtonBox::Apply))
        {
            applyButton->setEnabled(false);
        }
    }

    m_SupportTargetViewControlSelection = (targetSelector != nullptr);
    if (m_SupportTargetViewControlSelection)
    {
        initializeTargetViewControlComboBox(targetSelector);
    }

    m_SupportPerChannelProcessing = options.supportPerChannel;
    if (m_SupportPerChannelProcessing)
    {
        initializeListOfSupportedChannelslComboBox(channelSelector);
    }

    m_ShowWaitingSpinner = options.showWaitingSpinner;
    m_impl->m_PreviewAreaSelectionEnabled = options.previewAreaSelectionEnabled;
    if (!m_impl->m_PreviewAreaSelectionEnabled)
    {
        m_rt_preview_use_sub_area = false;
        m_rt_preview_sub_area = {};
    }

    if (!isPreviewEnabled())
    {
        if (!m_impl->m_PreviewOffWindowConstraintsSaved)
        {
            m_impl->m_SavedWindowMaxSize = an::ui::sanitizeMaxSize(maximumSize());
            m_impl->m_PreviewOffWindowConstraintsSaved = true;
        }

        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

        if (auto* splitter = an::ui::findMainProcessSplitter(m_MainHorizontalLayout))
        {
            an::ui::setPreviewPaneVisible(splitter, false);
            an::ui::collapsePreviewPane(splitter);
        }

        if (auto* previewPanel = an::ui::findPreviewPanelWidget(m_ImagePreviewLayout))
        {
            previewPanel->setVisible(false);
            previewPanel->setMaximumWidth(0);
            previewPanel->setMinimumWidth(0);
        }
    }
}

void ImageRTPreviewControl::changeTargetViewControl(ImageViewControl* control)
{
    if (!control)
        return;

    cancelPreviews();
    ImageProcessControl::changeTargetViewControl(control);
    // What frames made of the old target is never found for the new one; its memory goes now.
    m_impl->m_FrameProxies.clear();
    m_impl->m_Codes.reset();
    m_impl->m_KeptPreviews.reset();
    releaseFrameConversions();

    if (m_SupportExecutionAutostretch && m_rt_preview_view_control)
    {
        m_rt_preview_view_control->enableRTPreviewAutostretchControls(true);

        auto* viewWidget = m_rt_preview_view_control->getViewWidget();
        if (viewWidget)
        {
            viewWidget->enableAutostretchStatisticsCaching(true);
        }

        invalidateExecutionAutostretchStatistics();
        updateAlternateImage(false);

        // Keep preview autostretch settings in sync with the target view control.
        if (m_rt_preview_view_control->isAutoStretchEnabled() && m_image_view_control && viewWidget)
        {
            viewWidget->setAutostretchSettings(m_image_view_control->getAutostretchSettings(), false);
        }
    }

    if (isPreviewEnabled())
    {
        m_FitToWindowRequested = true;
        requestPreview(an::ui::PreviewFrameKind::Settled);
    }
}

bool ImageRTPreviewControl::isRTPreviewProcess() const
{
    return true;
}

bool ImageRTPreviewControl::requiresExclusivePreviewAccess() const
{
    return true;
}

void ImageRTPreviewControl::disableRealtimePreview()
{
    disableRTPreview();
}

void ImageRTPreviewControl::onImageLoaded(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageLoaded(ctrl);

    if (ctrl == m_image_view_control)
    {
        invalidateExecutionAutostretchStatistics();
        releaseFrameCaches();

        if (isPreviewEnabled() && !m_IgnoreLastImageUpdatedEvent)
        {
            requestPreview(an::ui::PreviewFrameKind::Settled);
        }
    }
    else if (ctrl != nullptr && ctrl == m_rt_preview_view_control && m_CropController)
    {
        // A view loading an image with no plate solution turns its hover tracking off - the preview view does at its
        // first frame and at every identity frame - and the preview area's resize cursors need it.
        m_CropController->setListenerEnabled(true);
    }
}

void ImageRTPreviewControl::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);

    if (ctrl == m_image_view_control)
    {
        invalidateExecutionAutostretchStatistics();
        releaseFrameCaches();

        if (isPreviewEnabled() && !m_IgnoreLastImageUpdatedEvent)
        {
            requestPreview(an::ui::PreviewFrameKind::Settled);
        }

        if (m_rt_preview_view_control)
        {
            updateAlternateImage(true);
        }

        if (!isPreviewEnabled() && m_UseHistogramPreviewWidget && m_HistogramPreview && m_image_view_control)
        {
            m_HistogramPreview->setImage(m_image_view_control->document().asset());
        }
    }
}

void ImageRTPreviewControl::onImageRenamed(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageRenamed(ctrl);

    if (m_rt_preview_view_control)
    {
        m_rt_preview_view_control->setUniqueName(getWindowTitle());
    }
}

void ImageRTPreviewControl::onMaskUpdated(ImageViewControl* ctrl)
{
    onImageUpdated(ctrl);
}

void ImageRTPreviewControl::onAlternateImageEnabled(ImageViewControl* ctrl)
{
    if (m_UseHistogramPreviewWidget && m_HistogramPreview && isPreviewEnabled())
    {
        m_HistogramPreview->setImage(getAlternateAsset());
    }
}

void ImageRTPreviewControl::onAlternateImageDisabled(ImageViewControl* ctrl)
{
    if (m_UseHistogramPreviewWidget && m_HistogramPreview && isPreviewEnabled() && m_rt_preview_view_control)
    {
        m_HistogramPreview->setImage(m_rt_preview_view_control->document().asset());
    }
}

void ImageRTPreviewControl::onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode)
{
    if (m_UseHistogramPreviewWidget && m_HistogramPreview && isPreviewEnabled())
    {
        ImageCompositionMode cMode = (ImageCompositionMode)mode;
        if (cMode == ImageCompositionMode::r_mode)
        {
            m_HistogramPreview->setDisplayedChannels(true, false, false);
        }
        else if (cMode == ImageCompositionMode::g_mode)
        {
            m_HistogramPreview->setDisplayedChannels(false, true, false);
        }
        else if (cMode == ImageCompositionMode::b_mode)
        {
            m_HistogramPreview->setDisplayedChannels(false, false, true);
        }
        else
        {
            m_HistogramPreview->setDisplayedChannels(true, true, true);
        }
    }
}

void ImageRTPreviewControl::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    if (!isPreviewEnabled() && m_UseHistogramPreviewWidget && m_HistogramPreview && m_image_view_control)
    {
        m_HistogramPreview->setImage(m_image_view_control->document().asset());
    }

    if (m_rt_preview_view_control)
    {
        invalidateExecutionAutostretchStatistics();

        if (m_rt_preview_use_sub_area)
        {
            resetPreviewSubAreaToDefault();
            recreatePreviewSubAreaController();
        }
        else
        {
            clearPreviewSubAreaOverlay();
        }

        updateAlternateImage(true);
    }
}

void ImageRTPreviewControl::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control)
    {
        // Nothing on its way over the closed image may publish.
        cancelPreviews();
        m_impl->m_Runs.cancel();
    }

    ImageProcessControl::onImageViewControlDestroyed(ctrl);

    if (ctrl == m_rt_preview_view_control)
    {
        destroyPreviewSubAreaController();
        m_rt_preview_view_control = nullptr;
        enablePreview(false);
    }
}

void ImageRTPreviewControl::onRTTakeSnapshot(ImageViewControl* ctrl)
{
    if (ctrl == m_rt_preview_view_control && m_image_view_control)
    {
        takeSnapshot(fmt::format("{} - {} - SNAPSHOT", m_image_view_control->getUniqueName(), getProcessName()));
    }
}

void ImageRTPreviewControl::selectionAreaUpdated() {}

void ImageRTPreviewControl::selectionAreaChanged()
{
    if (!m_rt_preview_view_control)
    {
        return;
    }

    m_rt_preview_sub_area =
        normalizedPreviewSubArea(m_rt_preview_view_control->getViewWidget()->getCurrentSelectionArea());
    ensurePreviewSubAreaInitialized();
    updatePreviewSubAreaOverlay();

    onImageUpdated(m_image_view_control);
}

void ImageRTPreviewControl::newSelectionFinished() {}

/// Programmatically disables the real-time preview.  Called when the
/// preview's backing ImageViewControl is destroyed externally.
///
/// The window-resize strategy mirrors the unchecked branch of
/// on_rt_preview_clicked — see its documentation for a detailed
/// explanation of the setFixedSize / deferred-unlock technique.
void ImageRTPreviewControl::disableRTPreview()
{
    if (m_rt_preview_view_control)
    {
        int leftPaneWidth = -1;
        int shellExtraWidth = 0;

        if (auto* splitter = an::ui::findMainProcessSplitter(m_MainHorizontalLayout))
        {
            const QList<int> sizes = splitter->sizes();
            if (sizes.size() > 1 && sizes[1] > 0)
            {
                m_impl->m_LastPreviewSplitterSizes = sizes;
                m_impl->m_HasLastPreviewSplitterSizes = true;
            }

            if (!sizes.isEmpty())
            {
                leftPaneWidth = (std::max)(0, sizes[0]);
                shellExtraWidth = (std::max)(0, width() - splitter->width());
            }
        }

        m_impl->m_LastPreviewWindowSize = size();
        m_impl->m_HasLastPreviewWindowSize = true;

        // Save constraints BEFORE setting the flag, so the guard below works.
        if (!m_impl->m_PreviewOffWindowConstraintsSaved)
        {
            m_impl->m_SavedWindowMaxSize = an::ui::sanitizeMaxSize(maximumSize());
            if (auto* lay = layout())
            {
                m_impl->m_SavedLayoutConstraint = lay->sizeConstraint();
            }
            m_impl->m_PreviewOffWindowConstraintsSaved = true;
        }

        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

        cancelPreviews();

        destroyPreviewControl();

        if (auto* splitter = an::ui::findMainProcessSplitter(m_MainHorizontalLayout))
        {
            an::ui::setPreviewPaneVisible(splitter, false);
        }

        if (auto* lay = layout())
        {
            lay->setSizeConstraint(QLayout::SetDefaultConstraint);
        }

        if (leftPaneWidth > 0)
        {
            setFixedSize(leftPaneWidth + shellExtraWidth, height());
        }
        else
        {
            setFixedSize(sizeHint().width(), height());
        }

        QTimer::singleShot(0, this, [this]() {
            setMinimumSize(0, 0);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        });

        enablePreview(false);
    }
}

void ImageRTPreviewControl::onStopWorkInFlight()
{
    ImageProcessControl::onStopWorkInFlight();
    m_impl->m_Previews.cancel();
    m_impl->m_Runs.cancel();
}

ImageRTPreviewControl::~ImageRTPreviewControl() noexcept
{
    m_impl->m_Destroying = true;
    // Whatever is still on its way is refused; nothing here waits for it.
    m_impl->m_Previews.cancel();
    m_impl->m_Runs.cancel();

    try
    {
        destroyPreviewControl(true);
    }
    catch (...)
    {
        // Destructors should not throw
    }
}

void ImageRTPreviewControl::on_rt_preview_clicked(Qt::CheckState state)
{
    if (state == Qt::CheckState::Checked)
    {
        // -----------------------------------------------------------
        //  RE-ENABLE PREVIEW
        // -----------------------------------------------------------
        // Restore the window constraints that were saved when the
        // preview was disabled.  This includes the max-size and,
        // critically, the layout's sizeConstraint (SetMinimumSize)
        // which prevents the controls from overlapping when the user
        // resizes the window.
        if (m_impl->m_PreviewOffWindowConstraintsSaved)
        {
            QSize maxSize = an::ui::sanitizeMaxSize(m_impl->m_SavedWindowMaxSize);
            setMaximumSize(maxSize);

            // Restore the layout constraint that was active before the
            // preview was hidden (typically SetMinimumSize).
            if (auto* lay = layout())
            {
                lay->setSizeConstraint(m_impl->m_SavedLayoutConstraint);
            }

            m_impl->m_PreviewOffWindowConstraintsSaved = false;
        }

        if (auto* previewPanel = an::ui::findPreviewPanelWidget(m_ImagePreviewLayout))
        {
            previewPanel->setMinimumWidth(kProcessMinPreviewPaneWidth);
            previewPanel->setMaximumWidth(QWIDGETSIZE_MAX);
            previewPanel->setVisible(true);
        }

        createPreviewControl();

        if (auto* splitter = an::ui::findMainProcessSplitter(m_MainHorizontalLayout))
        {
            an::ui::setPreviewPaneVisible(splitter, true);

            if (m_impl->m_HasLastPreviewSplitterSizes && m_impl->m_LastPreviewSplitterSizes.size() >= 2)
            {
                splitter->setSizes(m_impl->m_LastPreviewSplitterSizes);
            }
        }

        // Resize after the splitter and preview pane are fully restored so
        // the layout minimum already accounts for the visible preview.
        if (m_impl->m_HasLastPreviewWindowSize)
        {
            resize(m_impl->m_LastPreviewWindowSize);
        }
        else
        {
            adjustSize();
        }
    }
    else
    {
        // -----------------------------------------------------------
        //  DISABLE PREVIEW — window-resize strategy
        // -----------------------------------------------------------
        // Goal: shrink the window so that only the controls panel is
        //       visible, keeping the controls panel at its *current*
        //       width (not its sizeHint, which may be wider).
        //
        // Problem: Qt's SetMinimumSize constraint makes the window's
        //   effective minimum equal to the layout's minimumSize(),
        //   which is computed from the children's minimumSizeHint().
        //   The splitter's minimumSizeHint still includes the preview
        //   pane (it hasn't physically relaid out yet) — so a plain
        //   resize() is clamped to the old, wider floor.
        //
        // Solution (3 steps):
        //   1. Drop the layout constraint to SetDefaultConstraint so
        //      the layout no longer auto-enforces a minimum.
        //   2. Use setFixedSize() instead of resize().  setFixedSize()
        //      overrides *both* minimumSize and minimumSizeHint,
        //      bypassing the stale floor entirely.
        //   3. On the next event-loop iteration (QTimer::singleShot(0))
        //      unlock the fixed size so the window becomes user-
        //      resizable again.  At this point the splitter has
        //      settled and the layout minimum is correct.
        //
        // The layout constraint (SetMinimumSize) is restored when the
        // preview is re-enabled (checked branch above).
        // -----------------------------------------------------------

        int leftPaneWidth = -1;
        int shellExtraWidth = 0;

        if (auto* splitter = an::ui::findMainProcessSplitter(m_MainHorizontalLayout))
        {
            const QList<int> sizes = splitter->sizes();
            if (sizes.size() > 1 && sizes[1] > 0)
            {
                m_impl->m_LastPreviewSplitterSizes = sizes;
                m_impl->m_HasLastPreviewSplitterSizes = true;
            }

            if (!sizes.isEmpty())
            {
                leftPaneWidth = (std::max)(0, sizes[0]);
                shellExtraWidth = (std::max)(0, width() - splitter->width());
            }
        }

        m_impl->m_LastPreviewWindowSize = size();
        m_impl->m_HasLastPreviewWindowSize = true;

        if (!m_impl->m_PreviewOffWindowConstraintsSaved)
        {
            m_impl->m_SavedWindowMaxSize = an::ui::sanitizeMaxSize(maximumSize());
            if (auto* lay = layout())
            {
                m_impl->m_SavedLayoutConstraint = lay->sizeConstraint();
            }
            m_impl->m_PreviewOffWindowConstraintsSaved = true;
        }

        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

        cancelPreviews();

        destroyPreviewControl();

        if (auto* splitter = an::ui::findMainProcessSplitter(m_MainHorizontalLayout))
        {
            an::ui::setPreviewPaneVisible(splitter, false);
        }

        // Step 1: drop the constraint (see strategy comment above).
        if (auto* lay = layout())
        {
            lay->setSizeConstraint(QLayout::SetDefaultConstraint);
        }

        // Step 2: force the window to the target size.
        if (leftPaneWidth > 0)
        {
            setFixedSize(leftPaneWidth + shellExtraWidth, height());
        }
        else
        {
            setFixedSize(sizeHint().width(), height());
        }

        // Step 3: defer unlocking so the splitter settles first.
        QTimer::singleShot(0, this, [this]() {
            setMinimumSize(0, 0);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        });
    }
}

void ImageRTPreviewControl::setExecutionAutostretchSupported(bool supported)
{
    m_SupportExecutionAutostretch = supported;
    invalidateExecutionAutostretchStatistics();

    if (m_rt_preview_view_control)
    {
        auto* viewWidget = m_rt_preview_view_control->getViewWidget();

        if (m_SupportExecutionAutostretch)
        {
            m_rt_preview_view_control->enableRTPreviewAutostretchControls(true);
            if (viewWidget)
            {
                viewWidget->enableAutostretchStatisticsCaching(true);
            }
        }
        else
        {
            m_rt_preview_view_control->forceLinearMode();
            m_rt_preview_view_control->enableRTPreviewAutostretchControls(false);

            if (viewWidget)
            {
                viewWidget->enableAutostretchStatisticsCaching(true);
                viewWidget->setPrecomputedAnalysis(std::nullopt);
            }
        }

        updateAlternateImage(false);
    }
}

bool ImageRTPreviewControl::isExecutionAutostretchSupported() const
{
    return m_SupportExecutionAutostretch;
}

void ImageRTPreviewControl::on_rt_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->close();
    }
    else if (sb == QDialogButtonBox::Apply || sb == QDialogButtonBox::Ok)
    {
        applyProcess();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        // Every request the reset makes collapses into one preview of the parameters it leaves.
        holdPreviewRequests();
        resetSettings();
        enableProcessControls(true);
        releasePreviewRequests();
    }
}

void ImageRTPreviewControl::applyProcess()
{
    if (m_ExecutionInProgress || !m_image_view_control || !m_image_view_control->hasImage())
    {
        return;
    }

    if (!m_image_view_control->tryRequestLockForProcessing())
    {
        m_image_view_control->showViewControlBusyWarning(QString::fromUtf8(getProcessName()));
        return;
    }

    // A preview on its way holds the click back no longer: it is refused, and the Apply goes ahead.
    cancelPreviews();

    enableProcessControls(false);

    const bool sequence = m_image_view_control->isSequence();
    ProgressDialogControl* progress = getProgressDialogControl(sequence);
    if (sequence && !progress)
    {
        progress = new ProgressDialogControl(
            this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), m_image_view_control->getUniqueName())));
        progress->setProgressBarRange(0, 100);
        progress->setValue(0);
        progress->setText("Executing process for sequence...");
    }
    if (progress)
    {
        progress->show();
        progress->setText("Preparing execution...");
        registerNewProgressDialog(progress);
    }

    bool createNewImage = false;
    bool closeAtEnd = false;
    bool executeInBackground = true;
    std::string newImageName;
    getBatchExecutionParameters(createNewImage, closeAtEnd, executeInBackground, newImageName);

    m_SuspendBackgroundUpdates = true;
    m_IgnoreLastImageUpdatedEvent = true;

    const an::ui::ProcessRunKind kind =
        sequence ? an::ui::ProcessRunKind::SequenceFrame
                 : (createNewImage ? an::ui::ProcessRunKind::NewImage : an::ui::ProcessRunKind::Apply);
    std::shared_ptr<RunSnapshot> snapshot = snapshotRun(kind, getProcessParamsForExecution());
    if (sequence)
    {
        runOverSequence(std::move(snapshot), progress, createNewImage, newImageName, closeAtEnd);
    }
    else if (createNewImage)
    {
        runToNewImage(std::move(snapshot), progress, newImageName, closeAtEnd);
    }
    else
    {
        runApplyInPlace(std::move(snapshot), progress, closeAtEnd);
    }

    m_IgnoreLastImageUpdatedEvent = false;
}

std::shared_ptr<ImageRTPreviewControl::RunSnapshot> ImageRTPreviewControl::snapshotRun(an::ui::ProcessRunKind kind,
                                                                                       ProcessExecutionParamsPtr params)
{
    auto snapshot = std::make_shared<RunSnapshot>();
    snapshot->run.kind = kind;
    snapshot->run.channel = selectedChannelScope();
    snapshot->params = std::move(params);
    snapshot->run.coverage = processBodyCoverage(snapshot->params.get());
    snapshot->modulation = std::clamp(getModulationFromParams(snapshot->params.get()), 0.0, 1.0);
    snapshot->step = describeProcessStep(snapshot->params.get());
    if (!m_image_view_control)
    {
        return snapshot;
    }
    snapshot->target = m_image_view_control->document().asset();
    if (m_SupportMasking && m_image_view_control->isMasked() && m_image_view_control->isMaskEnabled())
    {
        if (ImageViewControl* mask = m_image_view_control->getCurrentMask(); mask && mask->hasImage())
        {
            snapshot->mask = mask->document().asset();
            snapshot->invertMask = m_image_view_control->isMaskInverted();
        }
    }
    snapshot->reads->name(snapshot->target);
    snapshot->reads->name(snapshot->mask);
    for (std::shared_ptr<const an::consumer::ImageAsset>& image : imagesRead(snapshot->params.get()))
    {
        snapshot->reads->name(std::move(image));
    }
    if (snapshot->run.isForPreview() && m_rt_preview_use_sub_area && !m_rt_preview_sub_area.isEmpty())
    {
        snapshot->area = normalizedPreviewSubArea(m_rt_preview_sub_area);
    }
    if (const std::optional<an::ui::DocumentSequence> sequence = m_image_view_control->document().sequence();
        sequence.has_value())
    {
        snapshot->sequence = sequence;
        snapshot->autoDebayer = m_image_view_control->isSequenceAutodebayerEnabled();
    }
    return snapshot;
}

void ImageRTPreviewControl::on_rt_plotResolution_currentTextChanged(const QString& str)
{
    if (!m_HistogramPreview)
        return;

    if (const std::optional<an::ui::PreviewHistogramResolution> resolution = resolutionForLabel(str);
        resolution.has_value())
    {
        m_HistogramPreview->setResolution(*resolution);
        // A preview's frames make the histogram they show: the next one is made at the new resolution.
        if (isPreviewEnabled() && m_rt_preview_view_control && m_UseHistogramPreviewWidget)
        {
            requestPreview(an::ui::PreviewFrameKind::Settled);
        }
    }
}

void ImageRTPreviewControl::on_rt_gridClicked(bool checked)
{
    if (m_HistogramPreview)
        m_HistogramPreview->setGridEnabled(checked);
}

void ImageRTPreviewControl::on_rt_zoomInButton()
{
    if (m_HistogramPreview)
        m_HistogramPreview->zoomIn();
}

void ImageRTPreviewControl::on_rt_zoomOutButton()
{
    if (m_HistogramPreview)
        m_HistogramPreview->zoomOut();
}

void ImageRTPreviewControl::on_rt_zoomResetButton()
{
    if (m_HistogramPreview)
        m_HistogramPreview->resetZoom();
}

void ImageRTPreviewControl::on_rt_localChannelNormalization(bool checked)
{
    if (m_HistogramPreview)
        m_HistogramPreview->setLocalChannelNormalization(checked);
}

void ImageRTPreviewControl::on_rt_showClippingInfo(bool checked)
{
    if (m_HistogramPreview)
        m_HistogramPreview->setClippingInfoEnabled(checked);
}

void ImageRTPreviewControl::on_rt_showHistogram(bool checked)
{
    m_UseHistogramPreviewWidget = checked;

    if (m_HistogramPreview)
    {
        if (m_UseHistogramPreviewWidget)
        {
            std::shared_ptr<const an::consumer::ImageAsset> image;
            if (m_rt_preview_view_control && m_rt_preview_view_control->hasImage())
            {
                image = m_rt_preview_view_control->document().asset();
            }
            else if (m_image_view_control)
            {
                image = m_image_view_control->document().asset();
            }

            m_HistogramPreview->setImage(std::move(image));
        }
        else
        {
            m_HistogramPreview->setImage(nullptr);
        }

        m_HistogramPreview->resetZoom();
        m_HistogramPreview->setGridEnabled(true);
    }

    for (QWidget* widget : std::initializer_list<QWidget*>{
             m_HistogramPreviewScrollArea, m_PlotResolution, m_ZoomIn, m_ZoomOut, m_ZoomReset, m_GridButton,
             m_LocalChannelNormalization, m_ShowClippingInfo, m_ShowHistogramAreas})
    {
        if (widget)
        {
            widget->setEnabled(m_UseHistogramPreviewWidget);
        }
    }
}

void ImageRTPreviewControl::on_rt_showHistogramAreas(bool checked)
{
    if (m_HistogramPreview)
        m_HistogramPreview->setAreaModeEnabled(checked);
}

void ImageRTPreviewControl::on_rt_preview_area_toggled(bool checked)
{
    if (!m_impl->m_PreviewAreaSelectionEnabled)
    {
        return;
    }

    if (m_rt_preview_use_sub_area == checked)
    {
        return;
    }

    m_rt_preview_use_sub_area = checked;

    if (m_rt_preview_use_sub_area)
    {
        ensurePreviewSubAreaInitialized();
        recreatePreviewSubAreaController();
    }
    else
    {
        destroyPreviewSubAreaController();
        clearPreviewSubAreaOverlay();
    }

    updateAlternateImage(true);

    if (isPreviewEnabled())
    {
        requestPreview(an::ui::PreviewFrameKind::Settled);
    }
}

void ImageRTPreviewControl::closeEvent(QCloseEvent* event)
{
    m_ProcessCloseRequested = true;
    // Closing comes before draining: nothing on its way may publish from now on.
    cancelPreviews();
    m_impl->m_Runs.cancel();

    ImageProcessControl::closeEvent(event);
    if (event->isAccepted())
    {
        destroyPreviewControl(true);
    }
}

void ImageRTPreviewControl::onDraw(QPainter& painter) {}

void ImageRTPreviewControl::onDisplayModeChanged(ImageViewMode mode)
{
    if (!m_SupportExecutionAutostretch)
    {
        // A preview whose frames may run over a reduction of the target is stretched through the target's analysis,
        // which a frame measures: entering autostretch asks for one, so the view is never stretched by its own result.
        if (previewScaling() == an::ui::PreviewScaling::Decimated && mode == ImageViewMode::AUTOSTRETCH_MODE &&
            m_rt_preview_view_control && isPreviewEnabled())
        {
            requestPreview(an::ui::PreviewFrameKind::Settled);
        }
        return;
    }

    if (!m_rt_preview_view_control)
    {
        return;
    }

    auto* viewWidget = getViewWidget();
    if (!viewWidget)
    {
        return;
    }

    const bool autostretchEnabled = (mode == ImageViewMode::AUTOSTRETCH_MODE);
    viewWidget->enableAutostretchStatisticsCaching(true);
    invalidateExecutionAutostretchStatistics();

    updateAlternateImage(false);

    if (autostretchEnabled)
    {
        if (m_image_view_control)
        {
            // Use the same autostretch settings as the target view control to match its display.
            viewWidget->setAutostretchSettings(m_image_view_control->getAutostretchSettings(), false);
        }
        // The preview is presented again through the target's statistics.
        if (isPreviewEnabled())
        {
            requestPreview(an::ui::PreviewFrameKind::Settled);
        }
    }
    else
    {
        viewWidget->setPrecomputedAnalysis(std::nullopt);

        // If the alternate image is being displayed, refresh so it uses the updated alternate image pointer.
        if (viewWidget->isAlternateImageRenderEnabled())
        {
            viewWidget->imageUpdated(true, true);
        }
    }
}

void ImageRTPreviewControl::onAutostretchContextChanged()
{
    if (!m_SupportExecutionAutostretch)
    {
        // The target's analysis a reducible preview is stretched through is measured for the view's stretch settings:
        // new settings ask for a frame that measures it for them.
        if (previewScaling() == an::ui::PreviewScaling::Decimated && m_rt_preview_view_control &&
            m_rt_preview_view_control->isAutoStretchEnabled() && isPreviewEnabled())
        {
            requestPreview(an::ui::PreviewFrameKind::Settled);
        }
        return;
    }

    invalidateExecutionAutostretchStatistics();
    if (isPreviewEnabled())
    {
        requestPreview(an::ui::PreviewFrameKind::Settled);
    }
}

void ImageRTPreviewControl::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::ActivationChange && isActiveWindow() && m_rt_preview_view_control &&
        !m_ProcessCloseRequested && !m_impl->m_Destroying)
    {
        // A person who changed the frame rate on the settings page comes back to this window: the next chain of frames
        // is paced at what the store holds now. Asked from the event loop, never from inside whatever changed the
        // activation - a window going, say.
        QTimer::singleShot(0, this, [this]() {
            if (m_rt_preview_view_control && !m_ProcessCloseRequested && !m_impl->m_Destroying)
            {
                refreshFramePeriod();
            }
        });
    }
    if (e->type() == QEvent::WindowStateChange)
    {
        if (!isMinimized())
        {
            if (isPreviewEnabled() && m_rt_preview_view_control)
            {
                // Defer fitToWindow so it runs after the new geometry is applied.
                // WindowStateChange fires before the widget is resized, so calling
                // fitToWindow synchronously would use the old viewport dimensions.
                QTimer::singleShot(0, this, [this]() {
                    if (m_rt_preview_view_control && isPreviewEnabled() && !m_ProcessCloseRequested)
                    {
                        m_rt_preview_view_control->fitToWindow();
                    }
                });
            }
        }
    }

    e->accept();
}

void ImageRTPreviewControl::resetSettings()
{
    if (!isPreviewEnabled() && m_UseHistogramPreviewWidget && m_HistogramPreview && m_image_view_control)
    {
        m_HistogramPreview->setImage(m_image_view_control->document().asset());
    }
}

void ImageRTPreviewControl::cancelPreviews()
{
    m_impl->m_Previews.cancel();
    m_impl->m_WantedFrame.reset();
    m_impl->m_SettledFrameOwed = false;
    m_impl->m_SettleTimer.stop();
}

void ImageRTPreviewControl::schedulePreviewProcessExecution()
{
    // A change made without a held slider is answered at once by an interactive frame where one costs what the view
    // paints rather than the image - over the target reduced, or the part the view shows - and settled after the
    // settle delay; elsewhere it is one settled frame, which would cost what an interactive one does.
    const bool cheaperAtOnce = m_impl->m_ControlsHeld.empty() && interactiveFrameRunsOverView();
    requestPreview(!m_impl->m_ControlsHeld.empty() || cheaperAtOnce ? an::ui::PreviewFrameKind::Interactive
                                                                    : an::ui::PreviewFrameKind::Settled);
}

bool ImageRTPreviewControl::interactiveFrameRunsOverView() const
{
    const an::ui::PreviewScaling scaling = previewScaling();
    if (scaling == an::ui::PreviewScaling::FullOnly || !m_rt_preview_view_control ||
        (m_rt_preview_use_sub_area && !m_rt_preview_sub_area.isEmpty()))
    {
        return false;
    }
    const ImageViewWidget* widget = m_rt_preview_view_control->getViewWidget();
    const std::optional<an::ui::ViewPresentationSettings> shown =
        widget != nullptr ? widget->framePresentationSettings() : std::nullopt;
    return shown.has_value() &&
           (shown->region.has_value() || (scaling == an::ui::PreviewScaling::Decimated && shown->proxyFactor >= 2u));
}

void ImageRTPreviewControl::requestPreview(an::ui::PreviewFrameKind kind)
{
    if (!m_rt_preview_view_control || m_ProcessCloseRequested || m_impl->m_StartupPreviewPending)
    {
        return;
    }

    m_SuspendBackgroundUpdates = false;
    ++m_impl->m_Telemetry.previewsRequested;
    if (m_impl->m_PreviewHolds > 0)
    {
        // A reset is changing the parameters: its requests collapse into the one asked for when it ends.
        return;
    }
    if (kind == an::ui::PreviewFrameKind::Interactive)
    {
        // The drag is owed a settled frame when it ends - its control let go, or held still for the settle delay.
        m_impl->m_SettledFrameOwed = true;
        m_impl->m_SettleTimer.start();
        // A settled frame still on its way shows parameters the drag has left behind, over the whole target: it is
        // stopped, so the drag's frame over what the view paints starts now rather than after it, and the drag's own
        // settled frame follows the drag.
        if (m_impl->m_PreviewRunning && m_impl->m_RunningFrameStoppable && interactiveFrameRunsOverView())
        {
            m_impl->m_Previews.cancel();
            m_impl->m_RunningFrameStoppable = false;
        }
    }
    else
    {
        m_impl->m_SettleTimer.stop();
    }
    if (!m_impl->m_PreviewRunning && !m_impl->m_WantedFrame.has_value() && !m_impl->m_PreviewTimerArmed)
    {
        // A chain of frames begins: it is paced at the rate the settings last answered, and the store is asked again,
        // off the GUI thread, for the chains to come.
        m_impl->m_FramePeriod = m_impl->m_NextFramePeriod;
        refreshFramePeriod();
    }
    m_impl->m_WantedFrame = collapsedKind(m_impl->m_WantedFrame, kind);
    pumpPreview();
}

void ImageRTPreviewControl::pumpPreview()
{
    // While a frame runs or the pacing timer waits, the frame wanted waits with them; it starts, once, with the
    // parameters the window holds then.
    if (!m_impl->m_WantedFrame.has_value() || m_impl->m_PreviewRunning || m_impl->m_PreviewTimerArmed)
    {
        return;
    }
    // A frame starts no earlier than one period after the last one started.
    const std::chrono::nanoseconds period = m_impl->m_FramePeriod;
    const std::chrono::nanoseconds elapsed = m_impl->m_LastPreviewStart.isValid()
                                                 ? std::chrono::nanoseconds(m_impl->m_LastPreviewStart.nsecsElapsed())
                                                 : period;
    if (elapsed < period)
    {
        m_impl->m_PreviewTimerArmed = true;
        const auto wait = std::chrono::ceil<std::chrono::milliseconds>(period - elapsed);
        QTimer::singleShot(static_cast<int>(wait.count()), Qt::PreciseTimer, this, [this]() {
            m_impl->m_PreviewTimerArmed = false;
            pumpPreview();
        });
        return;
    }
    const an::ui::PreviewFrameKind kind = *m_impl->m_WantedFrame;
    m_impl->m_WantedFrame.reset();
    startPreview(kind);
}

void ImageRTPreviewControl::startPreview(an::ui::PreviewFrameKind kind)
{
    if (!m_rt_preview_view_control || m_ProcessCloseRequested || m_SuspendBackgroundUpdates || m_ExecutionInProgress ||
        !isPreviewEnabled() || !m_image_view_control || !m_image_view_control->hasImage())
    {
        return;
    }
    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr)
    {
        return;
    }
    if (kind == an::ui::PreviewFrameKind::Settled)
    {
        // This frame shows every change asked for before it with the parameters the window holds now.
        m_impl->m_SettledFrameOwed = false;
    }
    // Only a frame asked for as settled may be stopped by a drag; a drag's own frame that runs over the whole target is
    // the drag's latest, and stopping it would leave the drag showing nothing.
    const bool askedSettled = kind == an::ui::PreviewFrameKind::Settled;
    m_impl->m_LastPreviewStart.restart();

    ProcessExecutionParamsPtr params = getProcessParamsForExecution();
    if (isIdentity(params.get()))
    {
        // Nothing would change: the preview view shows the target itself, and the next frame is paced from now.
        showIdentityPreview();
        pumpPreview();
        return;
    }
    const std::string resultKey = previewResultKey(params.get());
    std::shared_ptr<RunSnapshot> snapshot = snapshotRun(an::ui::ProcessRunKind::Preview, std::move(params));
    snapshot->resultKey = resultKey;
    if (showKeptPreview(*snapshot))
    {
        // A frame made this already: the preview view shows it, and the next frame is paced from now.
        pumpPreview();
        return;
    }

    // The ticket captures the target's revision now, and the mask's: a preview of an image that changes under it is
    // refused when it lands.
    std::vector<const an::ui::ImageDocument*> secondary;
    if (m_SupportMasking && m_image_view_control->isMasked() && m_image_view_control->isMaskEnabled())
    {
        if (ImageViewControl* mask = m_image_view_control->getCurrentMask(); mask && mask->hasImage())
        {
            secondary.push_back(&mask->document());
        }
    }
    an::Result<an::ui::CommitTicket> ticket = m_impl->m_Previews.issue(m_image_view_control->document(), secondary);
    if (!ticket.hasValue())
    {
        return;
    }
    snapshot->ticket = std::move(*ticket);
    snapshot->run.frameKind = kind;
    if (kind == an::ui::PreviewFrameKind::Interactive)
    {
        // An interactive frame runs on what previews are sure of and borrows nothing past it; a settled one may.
        snapshot->maxConcurrency = an::consumer::previewReservationWidth(session->width());
    }
    if (!snapshot->area.isEmpty() && m_impl->m_CompositeBase &&
        m_impl->m_CompositeBaseSource == snapshot->ticket.source())
    {
        snapshot->keptBase = m_impl->m_CompositeBase;
    }
    // The preview of a process that stretches it from the target, or whose frames may run over a reduction of it, is
    // stretched through the target's analysis, measured once per revision: the stretch never moves under an adjustment,
    // and is never measured on a reduction.
    const an::ui::PreviewScaling scaling = previewScaling();
    const bool decimated = scaling == an::ui::PreviewScaling::Decimated;
    const bool nativeRegion = scaling == an::ui::PreviewScaling::NativeRegion;
    if ((m_SupportExecutionAutostretch || decimated) && m_rt_preview_view_control->isAutoStretchEnabled())
    {
        if (auto* widget = m_rt_preview_view_control->getViewWidget())
        {
            snapshot->statsSource = getExecutionAutostretchSource();
            snapshot->reads->name(snapshot->statsSource);
            if (snapshot->statsSource)
            {
                // Measured for what the preview view shows - one channel of a colour image, or the whole of it - as
                // the frame's presentation asks, which refuses an analysis of another.
                an::ui::ViewPresentationSettings settings;
                settings.request = widget->presentationRequest();
                settings.channel = widget->displayedChannel();
                snapshot->statsSettings = settings;
                const std::string policy = an::ui::viewPresentationKey(settings);
                if (m_impl->m_Analysis.has_value() && m_impl->m_AnalysisPolicy == policy &&
                    m_impl->m_AnalysisSource == snapshot->statsSource->revision())
                {
                    snapshot->keptAnalysis = m_impl->m_Analysis;
                }
                else if (std::optional<an::DisplayPresentationAnalysis> shown =
                             widget->keptAnalysisOf(snapshot->statsSource->revision(), settings);
                         shown.has_value())
                {
                    // The preview view measured it presenting the image itself, as it shows the target until a frame
                    // lands: the same measurement, taken over rather than taken again in the first frame of a drag.
                    m_impl->m_Analysis = shown;
                    m_impl->m_AnalysisPolicy = policy;
                    m_impl->m_AnalysisSource = snapshot->statsSource->revision();
                    snapshot->keptAnalysis = std::move(shown);
                }
            }
        }
    }

    // What the frame presents for the preview view, the part of the target it runs over, and how it makes the dialog's
    // histogram, all decided now.
    if (ImageViewWidget* widget = m_rt_preview_view_control->getViewWidget(); widget != nullptr)
    {
        snapshot->presentation = widget->framePresentationSettings();
    }
    const std::optional<an::ImageDescriptor> targetShape =
        snapshot->target ? snapshot->target->descriptor() : std::nullopt;
    // A frame over what the view paints is stretched through the target's analysis, never through one of itself.
    const bool stretchable = !m_rt_preview_view_control->isAutoStretchEnabled() || snapshot->statsSource != nullptr;
    if (kind == an::ui::PreviewFrameKind::Interactive && (decimated || nativeRegion) && snapshot->area.isEmpty() &&
        snapshot->presentation.has_value() && targetShape.has_value() && !targetShape->isMosaiced() && stretchable)
    {
        if (decimated && snapshot->presentation->proxyFactor >= 2u)
        {
            snapshot->frameSource = FrameSource::Reduced;
            snapshot->frameFactor = snapshot->presentation->proxyFactor;
            snapshot->keptReduction = m_impl->m_FrameProxies.find(
                snapshot->target->revision(), an::ui::PreviewProxyKind::Reduced, snapshot->frameFactor);
            if (snapshot->mask)
            {
                snapshot->keptMaskReduction = m_impl->m_FrameProxies.find(
                    snapshot->mask->revision(), an::ui::PreviewProxyKind::Reduced, snapshot->frameFactor);
            }
        }
        else if (snapshot->presentation->region.has_value())
        {
            // A body that reads at full scale runs over a part of the target at every reduction of the view; a
            // reducible one, once the view shows the target at full scale. While what the view paints is its own
            // presentation of its document under the frame's settings, the part is what the view shows, aligned to
            // the blocks of its reduction, and the frame is shown as a layer over what is painted; otherwise it is
            // the whole region the view presents, and the frame is the presentation the view paints.
            snapshot->frameSource = FrameSource::Region;
            const QRect bounds = assetBounds(snapshot->target);
            const an::ui::ViewPresentationSettings& presented = *snapshot->presentation;
            const an::ui::ViewRegion& region = *presented.region;
            const QRect presentedPart = QRect(static_cast<int>(region.x), static_cast<int>(region.y),
                                              static_cast<int>(region.width), static_cast<int>(region.height))
                                            .intersected(bounds);
            snapshot->cropPart = presentedPart;
            if (const ImageViewWidget* widget = m_rt_preview_view_control->getViewWidget(); widget != nullptr)
            {
                if (const std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion(); shown.has_value())
                {
                    const int factor = static_cast<int>((std::max)(1u, presented.proxyFactor));
                    const QRect blocks(0, 0, bounds.width() / factor * factor, bounds.height() / factor * factor);
                    const QRect part = alignedToBlocks(*shown, presented.proxyFactor).intersected(blocks);
                    if (!part.isEmpty() && widget->paintsDocumentOver(presented, part))
                    {
                        snapshot->cropPart = part;
                        snapshot->regionLayer = true;
                    }
                }
            }
            // The body reads the part grown by the process's reach around it, within the target; the frame shows the
            // part alone.
            const int margin =
                static_cast<int>((std::min)(previewRegionMargin(snapshot->params.get()), kMaxRegionMargin));
            snapshot->bodyPart = snapshot->cropPart.adjusted(-margin, -margin, margin, margin).intersected(bounds);
        }
    }
    // A frame of a body that reads at full scale that can run over no part of what the view shows - the view shows the
    // whole target, paints no frame, or would stretch one by its own statistics - or whose reach around the part takes
    // in the whole target runs over the whole target, which is what the settled frame a drag is owed runs: it is that
    // frame, on the threads a settled frame may take.
    if (kind == an::ui::PreviewFrameKind::Interactive && nativeRegion && snapshot->area.isEmpty() &&
        (snapshot->frameSource == FrameSource::Target || snapshot->bodyPart == assetBounds(snapshot->target)))
    {
        kind = an::ui::PreviewFrameKind::Settled;
        snapshot->run.frameKind = kind;
        snapshot->maxConcurrency = 0u;
        snapshot->frameSource = FrameSource::Target;
        snapshot->cropPart = QRect();
        snapshot->bodyPart = QRect();
        snapshot->regionLayer = false;
        m_impl->m_SettledFrameOwed = false;
    }
    // A body that admits work of its own would wait inside the frame for the threads the frame borrowed.
    if (bodyAdmitsWorkOfItsOwn())
    {
        snapshot->maxConcurrency = an::consumer::previewReservationWidth(session->width());
    }
    if (m_UseHistogramPreviewWidget && m_HistogramPreview && targetShape.has_value())
    {
        snapshot->histogramResolution = an::ui::histogramResolutionOf(m_HistogramPreview->resolution());
        if (snapshot->frameSource == FrameSource::Target)
        {
            snapshot->histogram = FrameHistogram::Counted;
        }
        else if (nativeRegion)
        {
            // Nothing a frame over a part of the target makes stands for the whole result's counts: a body that reads
            // at full scale cannot be run over a sample of the target, so the histogram shows what the settled frame
            // counts.
            snapshot->histogram = FrameHistogram::None;
        }
        else if (mapsEachChannelAlone() && !snapshot->mask && isStoredChannelScope(snapshot->run.channel) &&
                 an::ui::codeCountOf(targetShape->sampleType()) > 0u)
        {
            snapshot->histogram = FrameHistogram::Remapped;
            if (m_impl->m_Codes && m_impl->m_CodesSource == snapshot->target->revision())
            {
                snapshot->keptCodes = m_impl->m_Codes;
            }
        }
        else
        {
            snapshot->histogram = FrameHistogram::Sampled;
            // One pixel of each block the frame reduces - as many samples as the reduction it runs over - and never
            // more densely than the factor that leaves about a million, which a frame over a region at full scale
            // would otherwise sample every pixel of.
            snapshot->sampleFactor =
                (std::max)(snapshot->frameSource == FrameSource::Reduced ? snapshot->frameFactor : 1u,
                           an::ui::previewSampleFactor(targetShape->extent().width(), targetShape->extent().height()));
            snapshot->keptSample = m_impl->m_FrameProxies.find(
                snapshot->target->revision(), an::ui::PreviewProxyKind::Sampled, snapshot->sampleFactor);
            if (snapshot->mask)
            {
                snapshot->keptMaskSample = m_impl->m_FrameProxies.find(
                    snapshot->mask->revision(), an::ui::PreviewProxyKind::Sampled, snapshot->sampleFactor);
            }
        }
    }

    // The part of the target a frame over a part runs over - the preview area, the part the view shows, or the region
    // it presents - and the crops of the target and of its mask kept for it, which a frame over the same part runs over
    // again.
    if (!snapshot->area.isEmpty())
    {
        snapshot->cropPart = snapshot->area;
        snapshot->bodyPart = snapshot->area;
    }
    if (!snapshot->cropPart.isEmpty())
    {
        const an::ui::PreviewPart part = partOf(snapshot->bodyPart);
        snapshot->keptCrop = m_impl->m_FrameProxies.findCrop(snapshot->target->revision(), part);
        if (snapshot->mask)
        {
            snapshot->keptMaskCrop = m_impl->m_FrameProxies.findCrop(snapshot->mask->revision(), part);
        }
    }
    // An interactive frame over the preview area may show its patch alone over what the view paints, when that is the
    // view's presentation of its document under the frame's settings and the patch's stretch is that presentation's:
    // none, or the analysis of the image the preview is stretched by.
    if (kind == an::ui::PreviewFrameKind::Interactive && !snapshot->area.isEmpty() &&
        snapshot->presentation.has_value() &&
        (!presentationMeasures(snapshot->presentation->request) || snapshot->statsSource != nullptr))
    {
        const ImageViewWidget* widget = m_rt_preview_view_control->getViewWidget();
        snapshot->patchFrame = widget != nullptr && widget->paintsDocumentAs(*snapshot->presentation);
    }
    if (snapshot->patchFrame && snapshot->histogram == FrameHistogram::Counted)
    {
        if (m_impl->m_TargetCounts && m_impl->m_TargetCountsSource == snapshot->target->revision() &&
            m_impl->m_TargetCountsResolution == snapshot->histogramResolution)
        {
            snapshot->keptTargetCounts = m_impl->m_TargetCounts;
        }
        if (snapshot->keptCrop && m_impl->m_CropCounts &&
            m_impl->m_CropCountsSource == snapshot->keptCrop->revision() &&
            m_impl->m_CropCountsResolution == snapshot->histogramResolution)
        {
            snapshot->keptCropCounts = m_impl->m_CropCounts;
        }
    }
    snapshot->conversions = m_impl->m_Conversions.kept();

    m_impl->m_PreviewRunning = true;
    m_impl->m_RunningFrameStoppable = askedSettled;
    ++m_impl->m_Telemetry.previewsStarted;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    if (m_ShowWaitingSpinner && !m_impl->m_SpinnerShown)
    {
        // Each frame is given the whole delay: a chain of short frames shows no spinner.
        m_impl->m_SpinnerDelay.start();
    }
    executeProcessStarted(RTPreviewMode::AsyncPreview, false);

    const ResultHook ready = [this](const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) {
        imageReadyToRender(result, run);
    };
    std::shared_ptr<const RunSnapshot> shared = snapshot;
    runThenPublish(
        [session, shared, ready, this]() {
            RunResult result;
            // The body reads the snapshot's parameters on the worker, never a widget.
            const RunBody withParams = [this, shared](const an::ui::ProcessSource& source,
                                                      an::ui::ProcessDestination& destination,
                                                      const an::ui::ProcessRun& run, an::ui::ExecutionScope& scope) {
                return executeProcessImpl(source, destination, run, shared->params.get(), scope);
            };
            const std::stop_token stop = shared->ticket.stopToken();
            // Every image the run reads is leased before it asks to be admitted, one lease each and in one order: an
            // edit holds its view's writer while it waits to be admitted, so a run admitted first would hold a thread
            // the edit waits for while it waited for the edit.
            const an::Status taken = shared->reads->take(stop);
            const ReadsRelease letGo(*shared->reads);
            if (taken.failed())
            {
                result.status = taken;
                return result;
            }
            an::Result<an::consumer::AssetReadLease> lease = heldShare(*shared->reads, *shared->target);
            if (!lease.hasValue())
            {
                result.status = lease.status();
                return result;
            }
            an::Result<MaskRead> mask = readMask(shared->mask, shared->reads.get(), lease->revision().assetIdentity,
                                                 lease->image(), lease->validity(), stop);
            if (!mask.hasValue())
            {
                result.status = mask.status();
                return result;
            }
            // The stretch's analysis reads the target through the lease above, and another image through its own.
            std::optional<an::consumer::AssetReadLease> statsLease;
            if (shared->statsSource && shared->statsSettings.has_value() && !shared->keptAnalysis.has_value() &&
                shared->statsSource->revision().assetIdentity != lease->revision().assetIdentity)
            {
                if (an::Result<an::consumer::AssetReadLease> read = heldShare(*shared->reads, *shared->statsSource);
                    read.hasValue())
                {
                    statsLease.emplace(std::move(*read));
                }
            }
            const std::chrono::steady_clock::time_point asked = std::chrono::steady_clock::now();
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                *session,
                an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Preview,
                                         .maxConcurrency = shared->maxConcurrency,
                                         .stopToken = stop,
                                         .reads = shared->reads.get()},
                shared->observer);
            result.admissionWait = elapsedSince(asked);
            if (!scope.hasValue())
            {
                result.status = scope.status();
                return result;
            }
            // The region a frame over what the view shows at full scale runs over, within the target.
            const QRect region = shared->frameSource == FrameSource::Region ? shared->cropPart : QRect();
            // The mask's lease, for what is reduced or sampled of it: its own, or the target's when it is the target.
            const an::consumer::AssetReadLease* maskSource =
                mask->image == nullptr ? nullptr : (mask->lease.valid() ? &mask->lease : &*lease);
            const an::consumer::AssetRevision maskRevision = maskRevisionOf(*mask, *lease);
            // What earlier frames converted of what this one reads, and what it converts for the frames that follow:
            // every source the frame runs a body over carries them.
            an::ui::ProcessConversions conversions(scope->engine(), shared->conversions);
            const auto sourceOf = [&conversions](const an::consumer::AssetReadLease& read) {
                return an::ui::ProcessSource::of(read).reusing(&conversions);
            };
            // A part of the target or of its mask under the preview area or the region the view shows: the crop kept
            // for its revision and that part, or one cut now and handed back to be kept.
            const auto cropOf = [&](const std::shared_ptr<const an::consumer::ImageAsset>& kept,
                                    const an::consumer::AnyImage& image, const an::ValidityMask* validity,
                                    std::shared_ptr<const an::consumer::ImageAsset>& made)
                -> an::Result<std::shared_ptr<const an::consumer::ImageAsset>> {
                if (kept)
                {
                    ++result.cropsReused;
                    return kept;
                }
                an::Result<an::consumer::ImageAsset> cropped = an::ui::cropProcessRegion(
                    *session, scope->engine(), scope->execution(), image, validity, shared->bodyPart);
                if (!cropped.hasValue())
                {
                    return cropped.status();
                }
                made = std::make_shared<const an::consumer::ImageAsset>(std::move(*cropped));
                return made;
            };
            // What the dialog's histogram shows of the frame: the count of its whole result; the target's codes
            // remapped through what the body makes of every code; or the count of the body's result over the target
            // sampled one pixel per block - never a count of a reduction, whose averaging narrows every peak.
            const auto histogramOfFrame = [&]() -> an::Result<an::ui::HistogramDisplayData> {
                const an::ExecutionContext& execution = scope->execution();
                if (shared->histogram == FrameHistogram::Counted)
                {
                    if (!result.asset.has_value())
                    {
                        return refusal(an::ErrorCode::NotInitialized, "the frame made no whole result to count");
                    }
                    an::Result<an::consumer::AssetReadLease> shown = result.asset->read(stop);
                    if (!shown.hasValue())
                    {
                        return shown.status();
                    }
                    return an::ui::computeHistogramDisplayData(execution, *shown, shared->histogramResolution);
                }
                if (shared->histogram == FrameHistogram::Remapped)
                {
                    std::shared_ptr<const an::ui::CodeHistogram> codes = shared->keptCodes;
                    if (!codes)
                    {
                        an::Result<an::ui::CodeHistogram> counted = an::ui::countCodes(execution, *lease);
                        if (!counted.hasValue())
                        {
                            return counted.status();
                        }
                        codes = std::make_shared<const an::ui::CodeHistogram>(std::move(*counted));
                        result.newCodes = codes;
                    }
                    an::Result<an::consumer::ImageAsset> ramp = an::ui::codeRampOf(*session, scope->engine(), *lease);
                    if (!ramp.hasValue())
                    {
                        return ramp.status();
                    }
                    an::Result<an::consumer::AssetReadLease> rampLease = ramp->read(stop);
                    if (!rampLease.hasValue())
                    {
                        return rampLease.status();
                    }
                    // Every code mapped as the body maps it, the modulation applied: the row is a view of the target's
                    // codes, not the target, for a body that would measure it.
                    an::ui::ProcessRun rampRun = shared->run;
                    rampRun.partOfTarget = true;
                    an::Result<an::consumer::ImageAsset> mapped =
                        processOneImage(*session, *scope, an::ui::ProcessSource::of(*rampLease), nullptr, rampRun,
                                        shared->modulation, false, withParams, ResultHook{});
                    if (!mapped.hasValue())
                    {
                        return mapped.status();
                    }
                    an::Result<an::consumer::AssetReadLease> mappedLease = mapped->read(stop);
                    if (!mappedLease.hasValue())
                    {
                        return mappedLease.status();
                    }
                    return an::ui::remapHistogramDisplayData(scope->engine(), execution, *codes, *mappedLease,
                                                             shared->histogramResolution);
                }
                if (shared->histogram == FrameHistogram::Sampled)
                {
                    std::shared_ptr<const an::consumer::ImageAsset> sample = shared->keptSample;
                    if (!sample)
                    {
                        an::Result<an::consumer::ImageAsset> made = an::ui::sampleForPreview(
                            *session, scope->engine(), execution, *lease, shared->sampleFactor);
                        if (!made.hasValue())
                        {
                            return made.status();
                        }
                        sample = std::make_shared<const an::consumer::ImageAsset>(std::move(*made));
                        result.newSample = sample;
                    }
                    std::shared_ptr<const an::consumer::ImageAsset> maskSample = shared->keptMaskSample;
                    if (maskSource == &*lease)
                    {
                        maskSample = sample;
                    }
                    else if (maskSource != nullptr && !maskSample)
                    {
                        an::Result<an::consumer::ImageAsset> made = an::ui::sampleForPreview(
                            *session, scope->engine(), execution, *maskSource, shared->sampleFactor);
                        if (!made.hasValue())
                        {
                            return made.status();
                        }
                        maskSample = std::make_shared<const an::consumer::ImageAsset>(std::move(*made));
                        result.newMaskSample = maskSample;
                    }
                    an::Result<an::consumer::AssetReadLease> sampled = sample->read(stop);
                    if (!sampled.hasValue())
                    {
                        return sampled.status();
                    }
                    an::consumer::AssetReadLease sampledMask;
                    if (maskSample)
                    {
                        an::Result<an::consumer::AssetReadLease> read = maskSample->read(stop);
                        if (!read.hasValue())
                        {
                            return read.status();
                        }
                        sampledMask = std::move(*read);
                    }
                    an::ui::ProcessRun sampleRun = shared->run;
                    sampleRun.proxyFactor = shared->sampleFactor;
                    an::Result<an::consumer::ImageAsset> processed = processOneImage(
                        *session, *scope, sourceOf(*sampled), sampledMask.valid() ? &sampledMask.image() : nullptr,
                        sampleRun, shared->modulation, shared->invertMask, withParams, ResultHook{},
                        sampledMask.valid() ? sampledMask.revision() : an::consumer::AssetRevision{});
                    if (!processed.hasValue())
                    {
                        return processed.status();
                    }
                    an::Result<an::consumer::AssetReadLease> processedLease = processed->read(stop);
                    if (!processedLease.hasValue())
                    {
                        return processedLease.status();
                    }
                    return an::ui::computeHistogramDisplayData(execution, *processedLease, shared->histogramResolution);
                }
                return refusal(an::ErrorCode::InvalidState, "the frame was asked for no histogram");
            };
            // What the preview view paints of the frame, through the stretch the view shows it with: the target's
            // analysis when the preview is stretched by the target, otherwise one the presentation measures of the
            // whole result - never of a reduction or a region of it.
            const auto presentFrame = [&]() -> an::Result<an::ui::ViewPresentationOutcome> {
                const an::ui::ViewPresentationSettings& settings = *shared->presentation;
                const an::DisplayPresentationAnalysis* reuse =
                    shared->statsSource && result.analysis.has_value() ? &*result.analysis : nullptr;
                if (result.asset.has_value())
                {
                    an::Result<an::consumer::AssetReadLease> shown = result.asset->read(stop);
                    if (!shown.hasValue())
                    {
                        return shown.status();
                    }
                    std::shared_ptr<const an::ui::DisplayProxy> proxy;
                    if (settings.proxyFactor > 1u)
                    {
                        an::Result<std::shared_ptr<const an::ui::DisplayProxy>> built = an::ui::DisplayProxy::build(
                            scope->engine(), scope->execution(), *shown, settings.proxyFactor);
                        if (!built.hasValue())
                        {
                            return built.status();
                        }
                        proxy = *built;
                        result.resultProxy = proxy;
                    }
                    return an::ui::presentForView(scope->engine(), scope->execution(), *shown, settings, reuse,
                                                  &session->memory(), proxy.get());
                }
                if (!result.frameImage.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the frame made nothing to present");
                }
                const std::optional<an::ImageDescriptor> targetDescribed = shared->target->descriptor();
                if (!targetDescribed.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
                }
                an::Result<an::consumer::AssetReadLease> shown = result.frameImage->read(stop);
                if (!shown.hasValue())
                {
                    return shown.status();
                }
                // The frame's own result over the reduction presented as it is - all of it, or the region of the
                // reduction the view shows - and then stated as the view's presentation of the target would be: the
                // target's size, the factor, and the part of the target it covers.
                an::ui::ViewPresentationSettings own = settings;
                own.proxyFactor = 1u;
                const std::uint32_t factor = shared->frameFactor;
                if (settings.region.has_value())
                {
                    own.region = an::ui::ViewRegion{.x = settings.region->x / factor,
                                                    .y = settings.region->y / factor,
                                                    .width = (std::max)(1u, settings.region->width / factor),
                                                    .height = (std::max)(1u, settings.region->height / factor)};
                }
                an::Result<an::ui::ViewPresentationOutcome> presented = an::ui::presentForView(
                    scope->engine(), scope->execution(), *shown, own, reuse, &session->memory(), nullptr);
                if (!presented.hasValue())
                {
                    return presented.status();
                }
                presented->factor = factor;
                presented->sourceWidth = targetDescribed->extent().width();
                presented->sourceHeight = targetDescribed->extent().height();
                presented->covered = an::ui::ViewRegion{.x = presented->covered.x * factor,
                                                        .y = presented->covered.y * factor,
                                                        .width = presented->covered.width * factor,
                                                        .height = presented->covered.height * factor};
                return presented;
            };
            // An interactive frame over the preview area shown by its patch over what the view paints: the patch.
            std::optional<an::consumer::ImageAsset> patchImage;
            // Such a frame's histogram, from counts alone: the target's, less those of its part under the area, plus
            // the patch's - exactly the counts of the composite it shows - the first two kept for their revisions.
            const auto derivedHistogram =
                [&](const an::consumer::AssetReadLease& patch) -> an::Result<an::ui::HistogramDisplayData> {
                const an::ExecutionContext& execution = scope->execution();
                std::shared_ptr<const an::ui::HistogramDisplayData> whole = shared->keptTargetCounts;
                if (!whole)
                {
                    an::Result<an::ui::HistogramDisplayData> counted =
                        an::ui::computeHistogramDisplayData(execution, *lease, shared->histogramResolution);
                    if (!counted.hasValue())
                    {
                        return counted.status();
                    }
                    whole = std::make_shared<const an::ui::HistogramDisplayData>(std::move(*counted));
                    result.newTargetCounts = whole;
                }
                std::shared_ptr<const an::ui::HistogramDisplayData> part = shared->keptCropCounts;
                if (!part)
                {
                    const std::shared_ptr<const an::consumer::ImageAsset> crop =
                        result.newCrop ? result.newCrop : shared->keptCrop;
                    if (!crop)
                    {
                        return refusal(an::ErrorCode::InvalidState, "the frame cut no part of the target");
                    }
                    an::Result<an::consumer::AssetReadLease> cropLease = crop->read(stop);
                    if (!cropLease.hasValue())
                    {
                        return cropLease.status();
                    }
                    an::Result<an::ui::HistogramDisplayData> counted =
                        an::ui::computeHistogramDisplayData(execution, *cropLease, shared->histogramResolution);
                    if (!counted.hasValue())
                    {
                        return counted.status();
                    }
                    part = std::make_shared<const an::ui::HistogramDisplayData>(std::move(*counted));
                    result.newCropCounts = part;
                }
                an::Result<an::ui::HistogramDisplayData> replacement =
                    an::ui::computeHistogramDisplayData(execution, patch, shared->histogramResolution);
                if (!replacement.hasValue())
                {
                    return replacement.status();
                }
                return an::ui::replacedPartHistogram(*whole, *part, *replacement);
            };
            // The presentation of a part of the target's result - the preview area's patch, or a frame's part over what
            // the view shows: the part at the view's reduction, or what of it lies within the region the view presents
            // at full scale, through the stretch the view paints with and never one of its own, stated over the target.
            const auto presentPart = [&](const an::consumer::AssetReadLease& patch,
                                         const QRect& area) -> an::Result<an::ui::ViewPresentationOutcome> {
                const an::ui::ViewPresentationSettings& settings = *shared->presentation;
                an::ui::ViewPresentationSettings own = settings;
                own.proxyFactor = 1u;
                own.region.reset();
                std::shared_ptr<const an::ui::DisplayProxy> proxy;
                if (settings.proxyFactor > 1u && static_cast<std::uint32_t>(area.width()) >= settings.proxyFactor &&
                    static_cast<std::uint32_t>(area.height()) >= settings.proxyFactor)
                {
                    an::Result<std::shared_ptr<const an::ui::DisplayProxy>> built =
                        an::ui::DisplayProxy::build(scope->engine(), scope->execution(), patch, settings.proxyFactor);
                    if (!built.hasValue())
                    {
                        return built.status();
                    }
                    proxy = *built;
                    own.proxyFactor = settings.proxyFactor;
                }
                else if (settings.region.has_value())
                {
                    const an::ui::ViewRegion& shown = *settings.region;
                    const QRect visible = QRect(static_cast<int>(shown.x), static_cast<int>(shown.y),
                                                static_cast<int>(shown.width), static_cast<int>(shown.height))
                                              .intersected(area);
                    if (visible.isEmpty())
                    {
                        return refusal(an::ErrorCode::EmptySelection, "the preview view shows nothing of the part");
                    }
                    own.region = an::ui::ViewRegion{.x = static_cast<std::uint32_t>(visible.x() - area.x()),
                                                    .y = static_cast<std::uint32_t>(visible.y() - area.y()),
                                                    .width = static_cast<std::uint32_t>(visible.width()),
                                                    .height = static_cast<std::uint32_t>(visible.height())};
                }
                const an::DisplayPresentationAnalysis* reuse =
                    result.analysis.has_value() ? &*result.analysis : nullptr;
                an::Result<an::ui::ViewPresentationOutcome> presented = an::ui::presentForView(
                    scope->engine(), scope->execution(), patch, own, reuse, &session->memory(), proxy.get());
                if (!presented.hasValue())
                {
                    return presented.status();
                }
                if (presentationMeasures(settings.request) && !presented->analysisReused)
                {
                    return refusal(an::ErrorCode::InvalidState,
                                   "a part of the preview would be stretched by its own statistics");
                }
                const std::optional<an::ImageDescriptor> targetDescribed = shared->target->descriptor();
                if (!targetDescribed.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
                }
                presented->sourceWidth = targetDescribed->extent().width();
                presented->sourceHeight = targetDescribed->extent().height();
                presented->covered.x += static_cast<std::uint32_t>(area.x());
                presented->covered.y += static_cast<std::uint32_t>(area.y());
                return presented;
            };
            const auto run = [&]() -> an::Status {
                if (shared->frameSource == FrameSource::Reduced)
                {
                    // The body runs over the target reduced by the preview view's factor, the mask reduced with it.
                    const std::uint32_t factor = shared->frameFactor;
                    std::shared_ptr<const an::consumer::ImageAsset> reduction = shared->keptReduction;
                    if (!reduction)
                    {
                        an::Result<an::consumer::ImageAsset> made =
                            an::ui::reduceForPreview(*session, scope->engine(), scope->execution(), *lease, factor);
                        if (!made.hasValue())
                        {
                            return made.status();
                        }
                        reduction = std::make_shared<const an::consumer::ImageAsset>(std::move(*made));
                        result.newReduction = reduction;
                    }
                    std::shared_ptr<const an::consumer::ImageAsset> maskReduction = shared->keptMaskReduction;
                    if (maskSource == &*lease)
                    {
                        maskReduction = reduction;
                    }
                    else if (maskSource != nullptr && !maskReduction)
                    {
                        an::Result<an::consumer::ImageAsset> made = an::ui::reduceForPreview(
                            *session, scope->engine(), scope->execution(), *maskSource, factor);
                        if (!made.hasValue())
                        {
                            return made.status();
                        }
                        maskReduction = std::make_shared<const an::consumer::ImageAsset>(std::move(*made));
                        result.newMaskReduction = maskReduction;
                    }
                    an::Result<an::consumer::AssetReadLease> reduced = reduction->read(stop);
                    if (!reduced.hasValue())
                    {
                        return reduced.status();
                    }
                    an::consumer::AssetReadLease reducedMask;
                    if (maskReduction)
                    {
                        an::Result<an::consumer::AssetReadLease> read = maskReduction->read(stop);
                        if (!read.hasValue())
                        {
                            return read.status();
                        }
                        reducedMask = std::move(*read);
                    }
                    an::ui::ProcessRun reducedRun = shared->run;
                    reducedRun.proxyFactor = factor;
                    an::Result<an::consumer::ImageAsset> processed = processOneImage(
                        *session, *scope, sourceOf(*reduced), reducedMask.valid() ? &reducedMask.image() : nullptr,
                        reducedRun, shared->modulation, shared->invertMask, withParams, ready,
                        reducedMask.valid() ? reducedMask.revision() : an::consumer::AssetRevision{});
                    if (!processed.hasValue())
                    {
                        return processed.status();
                    }
                    result.frameImage.emplace(std::move(*processed));
                }
                else if (shared->frameSource == FrameSource::Region)
                {
                    // The body runs over the region the preview view shows at full scale, the mask cropped with it.
                    if (region.isEmpty())
                    {
                        return refusal(an::ErrorCode::EmptySelection, "the preview view shows nothing of the image");
                    }
                    an::Result<std::shared_ptr<const an::consumer::ImageAsset>> cropped =
                        cropOf(shared->keptCrop, lease->image(), lease->validity(), result.newCrop);
                    if (!cropped.hasValue())
                    {
                        return cropped.status();
                    }
                    an::Result<an::consumer::AssetReadLease> croppedLease = (*cropped)->read(stop);
                    if (!croppedLease.hasValue())
                    {
                        return croppedLease.status();
                    }
                    an::consumer::AssetReadLease croppedMaskLease;
                    if (mask->image != nullptr)
                    {
                        an::Result<std::shared_ptr<const an::consumer::ImageAsset>> maskCrop =
                            cropOf(shared->keptMaskCrop, *mask->image, mask->validity, result.newMaskCrop);
                        if (!maskCrop.hasValue())
                        {
                            return maskCrop.status();
                        }
                        an::Result<an::consumer::AssetReadLease> read = (*maskCrop)->read(stop);
                        if (!read.hasValue())
                        {
                            return read.status();
                        }
                        croppedMaskLease = std::move(*read);
                    }
                    an::ui::ProcessRun regionRun = shared->run;
                    regionRun.partOfTarget = true;
                    regionRun.part = shared->bodyPart;
                    an::Result<an::consumer::ImageAsset> processed = processOneImage(
                        *session, *scope, sourceOf(*croppedLease),
                        croppedMaskLease.valid() ? &croppedMaskLease.image() : nullptr, regionRun, shared->modulation,
                        shared->invertMask, withParams, ready,
                        croppedMaskLease.valid() ? croppedMaskLease.revision() : an::consumer::AssetRevision{});
                    if (!processed.hasValue())
                    {
                        return processed.status();
                    }
                    if (shared->bodyPart != shared->cropPart)
                    {
                        // What the body answered around the region is cut away: the frame shows the region alone.
                        an::Result<an::consumer::AssetReadLease> whole = processed->read(stop);
                        if (!whole.hasValue())
                        {
                            return whole.status();
                        }
                        an::Result<an::consumer::ImageAsset> shown = an::ui::cropProcessRegion(
                            *session, scope->engine(), scope->execution(), whole->image(), whole->validity(),
                            shared->cropPart.translated(-shared->bodyPart.topLeft()));
                        if (!shown.hasValue())
                        {
                            return shown.status();
                        }
                        result.frameImage.emplace(std::move(*shown));
                    }
                    else
                    {
                        result.frameImage.emplace(std::move(*processed));
                    }
                }
                else if (shared->area.isEmpty())
                {
                    an::Result<an::consumer::ImageAsset> processed = processOneImage(
                        *session, *scope, sourceOf(*lease), mask->image, shared->run, shared->modulation,
                        shared->invertMask, withParams, ready, maskRevision, &result.alternatives);
                    if (!processed.hasValue())
                    {
                        return processed.status();
                    }
                    result.asset.emplace(std::move(*processed));
                }
                else
                {
                    // The preview area: the source and the mask cropped by the engine once per revision and area, the
                    // body run over the crop, and the patch shown over what the view paints or pasted into the whole
                    // image's composite base.
                    an::Result<std::shared_ptr<const an::consumer::ImageAsset>> cropped =
                        cropOf(shared->keptCrop, lease->image(), lease->validity(), result.newCrop);
                    if (!cropped.hasValue())
                    {
                        return cropped.status();
                    }
                    an::Result<an::consumer::AssetReadLease> croppedLease = (*cropped)->read(stop);
                    if (!croppedLease.hasValue())
                    {
                        return croppedLease.status();
                    }
                    an::consumer::AssetReadLease croppedMaskLease;
                    if (mask->image != nullptr)
                    {
                        an::Result<std::shared_ptr<const an::consumer::ImageAsset>> maskCrop =
                            cropOf(shared->keptMaskCrop, *mask->image, mask->validity, result.newMaskCrop);
                        if (!maskCrop.hasValue())
                        {
                            return maskCrop.status();
                        }
                        an::Result<an::consumer::AssetReadLease> maskLease = (*maskCrop)->read(stop);
                        if (!maskLease.hasValue())
                        {
                            return maskLease.status();
                        }
                        croppedMaskLease = std::move(*maskLease);
                    }
                    an::ui::ProcessRun areaRun = shared->run;
                    areaRun.partOfTarget = true;
                    areaRun.part = shared->area;
                    an::Result<an::consumer::ImageAsset> patch = processOneImage(
                        *session, *scope, sourceOf(*croppedLease),
                        croppedMaskLease.valid() ? &croppedMaskLease.image() : nullptr, areaRun, shared->modulation,
                        shared->invertMask, withParams, ready,
                        croppedMaskLease.valid() ? croppedMaskLease.revision() : an::consumer::AssetRevision{});
                    if (!patch.hasValue())
                    {
                        return patch.status();
                    }
                    const std::optional<an::ImageDescriptor> patchDescribed = patch->descriptor();
                    const std::optional<an::ImageDescriptor> targetDescribed = shared->target->descriptor();
                    if (!patchDescribed.has_value() || !targetDescribed.has_value())
                    {
                        return refusal(an::ErrorCode::NotInitialized, "the preview's patch holds nothing");
                    }
                    if (patchDescribed->extent().width() != static_cast<std::uint32_t>(shared->area.width()) ||
                        patchDescribed->extent().height() != static_cast<std::uint32_t>(shared->area.height()))
                    {
                        return refusal(an::ErrorCode::IncompatibleArguments,
                                       "the process produced a preview area of another size than it was given");
                    }
                    // An interactive frame whose patch is the target's kind of image, every sample of it and of the
                    // target valid, is shown by the patch alone over what the view paints of the target or of the last
                    // settled composite - which outside the area is the target itself - and makes no composite.
                    bool layered = false;
                    {
                        an::Result<an::consumer::AssetReadLease> patchLease = patch->read(stop);
                        if (!patchLease.hasValue())
                        {
                            return patchLease.status();
                        }
                        layered = shared->patchFrame && patchIsTargetsKind(*patchLease, *targetDescribed) &&
                                  everySampleValid(*lease);
                        if (!layered)
                        {
                            const std::optional<an::ImageDescriptor> baseDescribed =
                                an::ui::processCompositeBaseDescriptor(*patchDescribed, *targetDescribed,
                                                                       scope->engine().limits());
                            if (!baseDescribed.has_value())
                            {
                                return refusal(an::ErrorCode::IncompatibleArguments,
                                               "the preview area cannot be shown over the whole image");
                            }
                            // The base of a patch of the target's own description over a target held a plane per
                            // channel would be a copy of the target, metadata and all, so the patch is pasted into a
                            // copy of the target itself; another base is built once per revision of the target and
                            // per description of its patches.
                            const an::consumer::AnyImage* baseImage = nullptr;
                            std::optional<an::consumer::AssetReadLease> baseLease;
                            if (*baseDescribed == *targetDescribed && isPlanar(lease->image()))
                            {
                                baseImage = &lease->image();
                            }
                            else
                            {
                                std::shared_ptr<const an::consumer::ImageAsset> base = shared->keptBase;
                                if (!base || base->descriptor() != baseDescribed)
                                {
                                    an::Result<an::consumer::ImageAsset> built = an::ui::buildProcessCompositeBase(
                                        *session, scope->engine(), scope->execution(), lease->image(),
                                        lease->validity(), *baseDescribed);
                                    if (!built.hasValue())
                                    {
                                        return built.status();
                                    }
                                    base = std::make_shared<const an::consumer::ImageAsset>(std::move(*built));
                                    result.newBase = base;
                                }
                                an::Result<an::consumer::AssetReadLease> read = base->read(stop);
                                if (!read.hasValue())
                                {
                                    return read.status();
                                }
                                baseLease.emplace(std::move(*read));
                                baseImage = &baseLease->image();
                            }
                            an::Result<an::consumer::ImageAsset> composite = an::ui::composeProcessRegion(
                                *session, scope->engine(), scope->execution(), *baseImage, patchLease->image(),
                                patchLease->validity(), shared->area.topLeft());
                            if (!composite.hasValue())
                            {
                                return composite.status();
                            }
                            result.asset.emplace(std::move(*composite));
                        }
                    }
                    if (layered)
                    {
                        patchImage.emplace(std::move(*patch));
                    }
                }
                // The stretch's analysis, measured once per revision of the image it is taken from.
                if (shared->statsSource && shared->statsSettings.has_value())
                {
                    if (shared->keptAnalysis.has_value())
                    {
                        result.analysis = shared->keptAnalysis;
                    }
                    else
                    {
                        // The target is measured through the lease this run holds; another image through its own,
                        // taken before the run was admitted.
                        const an::consumer::AssetReadLease* measured = &*lease;
                        if (shared->statsSource->revision().assetIdentity != lease->revision().assetIdentity)
                        {
                            measured = statsLease.has_value() ? &*statsLease : nullptr;
                        }
                        if (measured != nullptr)
                        {
                            an::Result<an::DisplayPresentationAnalysis> analysis = an::ui::analyzeForView(
                                scope->engine(), scope->execution(), *measured, *shared->statsSettings);
                            if (analysis.hasValue())
                            {
                                result.analysis = *analysis;
                                result.analysisMeasured = true;
                            }
                            else if (analysis.status().code() != an::ErrorCode::Cancelled)
                            {
                                // The frame is shown without the analysis it is stretched by - a reduced frame measured
                                // on its reduction, a whole frame on its own result - so the stretch may move.
                                LOG_WARNING("A preview frame's stretch analysis was not measured: {}",
                                            analysis.status().message());
                            }
                        }
                    }
                }
                // A patch shown over what the view paints is its frame's whole showing: its derived histogram and its
                // presentation.
                if (patchImage.has_value())
                {
                    an::Result<an::consumer::AssetReadLease> patch = patchImage->read(stop);
                    if (!patch.hasValue())
                    {
                        return patch.status();
                    }
                    if (shared->histogram != FrameHistogram::None)
                    {
                        an::Result<an::ui::HistogramDisplayData> derived = derivedHistogram(*patch);
                        if (derived.hasValue())
                        {
                            result.histogram =
                                std::make_shared<const an::ui::HistogramDisplayData>(std::move(*derived));
                            result.histogramDerived = true;
                        }
                        else if (derived.status().code() == an::ErrorCode::Cancelled)
                        {
                            return derived.status();
                        }
                        else
                        {
                            LOG_WARNING("A preview frame's histogram was not made: {}", derived.status().message());
                        }
                    }
                    an::Result<an::ui::ViewPresentationOutcome> presented = presentPart(*patch, shared->area);
                    if (!presented.hasValue())
                    {
                        return presented.status();
                    }
                    result.patchPresented = std::move(*presented);
                    return an::Status::success();
                }
                // What the dialog's histogram shows of the frame, made in the frame's own operation.
                if (shared->histogram != FrameHistogram::None)
                {
                    an::Result<an::ui::HistogramDisplayData> counted = histogramOfFrame();
                    if (counted.hasValue())
                    {
                        result.histogram = std::make_shared<const an::ui::HistogramDisplayData>(std::move(*counted));
                    }
                    else if (counted.status().code() == an::ErrorCode::Cancelled)
                    {
                        return counted.status();
                    }
                    else
                    {
                        LOG_WARNING("A preview frame's histogram was not made: {}", counted.status().message());
                    }
                }
                // What the preview view paints of the frame, presented in the frame's own operation. A frame over a
                // part of the target presents the part: shown as a layer over what the view paints, or as its
                // presentation of the whole region it presents.
                if (shared->presentation.has_value() && shared->frameSource == FrameSource::Region)
                {
                    if (!result.frameImage.has_value())
                    {
                        return refusal(an::ErrorCode::NotInitialized, "the frame made nothing to present");
                    }
                    an::Result<an::consumer::AssetReadLease> part = result.frameImage->read(stop);
                    if (!part.hasValue())
                    {
                        return part.status();
                    }
                    an::Result<an::ui::ViewPresentationOutcome> presented = presentPart(*part, shared->cropPart);
                    if (!presented.hasValue())
                    {
                        return presented.status();
                    }
                    if (shared->regionLayer)
                    {
                        result.patchPresented = std::move(*presented);
                    }
                    else
                    {
                        result.presented = std::move(*presented);
                    }
                }
                else if (shared->presentation.has_value())
                {
                    an::Result<an::ui::ViewPresentationOutcome> presented = presentFrame();
                    if (presented.hasValue())
                    {
                        result.presented = std::move(*presented);
                    }
                    else if (presented.status().code() == an::ErrorCode::Cancelled || result.frameImage.has_value())
                    {
                        // A frame only its presentation shows fails with it; a whole result the view presents itself.
                        return presented.status();
                    }
                }
                return an::Status::success();
            };
            const std::chrono::steady_clock::time_point working = std::chrono::steady_clock::now();
            result.status = run();
            result.work = elapsedSince(working);
            if (result.status.failed())
            {
                result.asset.reset();
                result.frameImage.reset();
                result.presented.reset();
                result.patchPresented.reset();
                result.histogram.reset();
            }
            result.conversionsMade = conversions.made();
            result.conversionsReused = conversions.reused();
            result.scopesPrepared = conversions.scopesPrepared();
            result.scopesReused = conversions.scopesReused();
            result.planarMade = conversions.planarMade();
            result.planarReused = conversions.planarReused();
            scope->finish(result.status);
            return result;
        },
        [this, shared, started](RunResult result) {
            m_impl->m_PreviewRunning = false;
            m_impl->m_RunningFrameStoppable = false;
            const an::ui::CommitVerdict verdict = shared->ticket.verdict();
            bool committed = false;
            bool adopted = false;
            // The revision of a result over the whole target handed to the preview view's document, if one was.
            std::optional<an::consumer::AssetRevision> landedWhole;
            // What the frame made of the target and its mask is filed by the revisions it was made from, and kept while
            // those are read: a frame a newer one superseded read the same revisions; one the target's or the mask's
            // change refused read revisions no frame reads again, and one that landed after the preview was turned off
            // would refill what the turn-off released.
            if (result.status.ok() &&
                (verdict == an::ui::CommitVerdict::Commit || verdict == an::ui::CommitVerdict::Superseded) &&
                m_rt_preview_view_control != nullptr)
            {
                an::ui::PreviewProxyCache& proxies = m_impl->m_FrameProxies;
                proxies.keep(shared->target->revision(), an::ui::PreviewProxyKind::Reduced, shared->frameFactor,
                             result.newReduction);
                proxies.keep(shared->target->revision(), an::ui::PreviewProxyKind::Sampled, shared->sampleFactor,
                             result.newSample);
                if (shared->mask)
                {
                    proxies.keep(shared->mask->revision(), an::ui::PreviewProxyKind::Reduced, shared->frameFactor,
                                 result.newMaskReduction);
                    proxies.keep(shared->mask->revision(), an::ui::PreviewProxyKind::Sampled, shared->sampleFactor,
                                 result.newMaskSample);
                }
                if (result.newCodes)
                {
                    m_impl->m_Codes = result.newCodes;
                    m_impl->m_CodesSource = shared->target->revision();
                }
                const an::ui::PreviewPart part = partOf(shared->bodyPart);
                proxies.keepCrop(shared->target->revision(), part, result.newCrop);
                if (shared->mask)
                {
                    proxies.keepCrop(shared->mask->revision(), part, result.newMaskCrop);
                }
                if (result.newTargetCounts)
                {
                    m_impl->m_TargetCounts = result.newTargetCounts;
                    m_impl->m_TargetCountsSource = shared->target->revision();
                    m_impl->m_TargetCountsResolution = shared->histogramResolution;
                }
                if (const std::shared_ptr<const an::consumer::ImageAsset> crop =
                        result.newCrop ? result.newCrop : shared->keptCrop;
                    result.newCropCounts && crop)
                {
                    m_impl->m_CropCounts = result.newCropCounts;
                    m_impl->m_CropCountsSource = crop->revision();
                    m_impl->m_CropCountsResolution = shared->histogramResolution;
                }
                m_impl->m_Conversions.keep(result.conversionsMade, result.conversionsReused);
                ProcessRunTelemetry& telemetry = m_impl->m_Telemetry;
                telemetry.scopesPrepared += result.scopesPrepared;
                telemetry.scopesReused += result.scopesReused;
                telemetry.planarConversionsMade += result.planarMade;
                telemetry.planarConversionsReused += result.planarReused;
                telemetry.cropsMade += (result.newCrop ? 1u : 0u) + (result.newMaskCrop ? 1u : 0u);
                telemetry.cropsReused += result.cropsReused;
                telemetry.compositeBasesBuilt += result.newBase ? 1u : 0u;
            }
            if (verdict == an::ui::CommitVerdict::Commit && result.status.ok() &&
                (result.asset.has_value() || result.frameImage.has_value() || result.patchPresented.has_value()) &&
                m_rt_preview_view_control && !m_ProcessCloseRequested)
            {
                ImageViewControl* preview = m_rt_preview_view_control;
                ImageViewWidget* widget = preview->getViewWidget();
                fitLandingPreview(*preview);
                // The frame's presentation is painted as it came, filed as the view's own would be for what its
                // document holds once this frame is published there.
                const auto adopt = [&]() {
                    if (result.presented.has_value() && shared->presentation.has_value() && widget != nullptr)
                    {
                        adopted =
                            widget->adoptPresentation(*shared->presentation, *result.presented, result.resultProxy);
                    }
                };
                if (result.patchPresented.has_value())
                {
                    // A frame over the preview area, or over the part of the target the view shows, is shown by its
                    // part painted over what the view paints; the document keeps what the last settled frame made.
                    if (shared->presentation.has_value() && widget != nullptr)
                    {
                        committed = widget->adoptPresentationLayer(*shared->presentation, *result.patchPresented);
                    }
                }
                else if (result.frameImage.has_value())
                {
                    // A frame over what the view paints of the target is shown by its presentation alone; the
                    // document keeps what the last settled frame made.
                    adopt();
                    committed = adopted;
                }
                else
                {
                    landedWhole = result.asset->revision();
                    // The view's document is read again after this block: a document of the preview's own replaces
                    // this one, which the view then lets go.
                    an::ui::ImageDocument& shown = preview->document();
                    if (!shown.valid() || shown.isShared())
                    {
                        // A preview that shows the target's own image - until its first frame lands, or after a frame
                        // that changed nothing - shows its result in a document of its own.
                        an::Result<an::ui::ImageDocument> first =
                            an::ui::ImageDocument::create(std::move(*result.asset));
                        if (first.hasValue())
                        {
                            preview->setDocument(std::move(*first));
                            adopt();
                            if (result.analysis.has_value())
                            {
                                preview->notifyImageUpdatedWithPresentationAnalysis(result.analysis, true);
                            }
                            committed = true;
                        }
                    }
                    else if (shown.tryReplace(std::move(*result.asset)).ok())
                    {
                        // The result waits for no reader of the image shown: what reads it - the view's own
                        // presentation, the lens, the statistics - goes on reading it.
                        adopt();
                        preview->notifyImageUpdatedWithPresentationAnalysis(result.analysis, true);
                        committed = true;
                    }
                    else
                    {
                        // A writer of the preview's document holds it: the replacement waits for the writer on a
                        // worker and is stretched with the analysis taken for it, as one taken at once is, and
                        // presented by the view once it is in place.
                        preview->replaceImageWithPresentationAnalysis(std::move(*result.asset), result.analysis, true);
                        committed = true;
                    }
                }
                if (committed)
                {
                    if (result.newBase)
                    {
                        m_impl->m_CompositeBaseSource = shared->ticket.source();
                        m_impl->m_CompositeBase = result.newBase;
                    }
                    if (result.analysisMeasured && result.analysis.has_value() && shared->statsSource &&
                        shared->statsSettings.has_value())
                    {
                        m_impl->m_Analysis = result.analysis;
                        m_impl->m_AnalysisSource = shared->statsSource->revision();
                        m_impl->m_AnalysisPolicy = an::ui::viewPresentationKey(*shared->statsSettings);
                        ++m_impl->m_Telemetry.analysesMeasured;
                    }
                    if (m_UseHistogramPreviewWidget && m_HistogramPreview)
                    {
                        // The histogram lands with the frame it describes; a frame that made none is counted here.
                        if (result.histogram)
                        {
                            m_HistogramPreview->showCounts(result.histogram);
                            if (result.histogramDerived)
                            {
                                ++m_impl->m_Telemetry.histogramsDerived;
                            }
                            else if (shared->histogram == FrameHistogram::Remapped)
                            {
                                ++m_impl->m_Telemetry.histogramsRemapped;
                            }
                            else if (shared->histogram == FrameHistogram::Sampled)
                            {
                                ++m_impl->m_Telemetry.histogramsSampled;
                            }
                            else
                            {
                                ++m_impl->m_Telemetry.histogramsCounted;
                            }
                        }
                        else if (!result.frameImage.has_value() && !result.patchPresented.has_value())
                        {
                            m_HistogramPreview->setImage(preview->document().asset());
                        }
                    }
                }
            }
            if (committed)
            {
                // A frame shown replaces what frames kept: one over the whole target keeps its own result under its
                // parameters' key, as the preview view's document holds it, and the images its body made beside it
                // under theirs.
                m_impl->m_KeptPreviews.reset();
                std::vector<std::pair<std::string, std::shared_ptr<const an::consumer::ImageAsset>>> results;
                if (landedWhole.has_value())
                {
                    std::shared_ptr<const an::consumer::ImageAsset> shown =
                        m_rt_preview_view_control->document().asset();
                    if (!shared->resultKey.empty() && shown && shown->revision() == *landedWhole)
                    {
                        results.emplace_back(shared->resultKey, std::move(shown));
                    }
                    for (an::ui::ProcessAlternative& alternative : result.alternatives)
                    {
                        results.emplace_back(
                            std::move(alternative.key),
                            std::make_shared<const an::consumer::ImageAsset>(std::move(alternative.image)));
                    }
                }
                if (!results.empty())
                {
                    m_impl->m_KeptPreviews = Impl::KeptPreviews{.target = shared->target->revision(),
                                                                .mask = shared->mask ? shared->mask->revision()
                                                                                     : an::consumer::AssetRevision{},
                                                                .masked = shared->mask != nullptr,
                                                                .invertMask = shared->invertMask,
                                                                .modulation = shared->modulation,
                                                                .channel = shared->run.channel,
                                                                .results = std::move(results)};
                }
            }
            if (committed)
            {
                const auto took =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started);
                ++m_impl->m_Telemetry.previewsCommitted;
                if (shared->run.frameKind == an::ui::PreviewFrameKind::Interactive)
                {
                    ++m_impl->m_Telemetry.interactivePreviewsCommitted;
                }
                else
                {
                    ++m_impl->m_Telemetry.settledPreviewsCommitted;
                }
                m_impl->m_Telemetry.previewTime += took;
                m_impl->m_Telemetry.longestPreview = (std::max)(m_impl->m_Telemetry.longestPreview, took);
                m_impl->m_Telemetry.frameWorkTime += result.work;
                m_impl->m_Telemetry.longestFrameWork = (std::max)(m_impl->m_Telemetry.longestFrameWork, result.work);
                m_impl->m_Telemetry.admissionWaitTime += result.admissionWait;
                m_impl->m_Telemetry.longestAdmissionWait =
                    (std::max)(m_impl->m_Telemetry.longestAdmissionWait, result.admissionWait);
                if (adopted)
                {
                    ++m_impl->m_Telemetry.presentationsAdopted;
                }
                if (shared->frameSource != FrameSource::Target)
                {
                    ++m_impl->m_Telemetry.viewFramesCommitted;
                    m_impl->m_Telemetry.viewFrameFactor = shared->frameFactor;
                }
                if (result.patchPresented.has_value() && shared->frameSource == FrameSource::Region)
                {
                    ++m_impl->m_Telemetry.viewFramesLayered;
                }
                else if (result.patchPresented.has_value())
                {
                    ++m_impl->m_Telemetry.patchFramesCommitted;
                }
                clearReportedPreviewFailure();
            }
            else if (verdict == an::ui::CommitVerdict::Commit && result.status.failed() &&
                     result.status.code() != an::ErrorCode::Cancelled)
            {
                ++m_impl->m_Telemetry.previewsFailed;
                reportPreviewFailure(result.status);
            }
            else
            {
                ++m_impl->m_Telemetry.previewsDiscarded;
            }
            executeProcessEnded(RTPreviewMode::AsyncPreview, false, committed);

            // The requests that came while it ran start one more frame, with the parameters held now.
            pumpPreview();
            settleSpinner();
        },
        kPreviewFramePriority, resultOfWorkThatThrew);
}

void ImageRTPreviewControl::showIdentityPreview()
{
    std::shared_ptr<const an::consumer::ImageAsset> target = m_image_view_control->document().asset();
    if (!target)
    {
        return;
    }
    if (showSharedPreview(target))
    {
        ++m_impl->m_Telemetry.identityPreviews;
    }
}

bool ImageRTPreviewControl::showSharedPreview(const std::shared_ptr<const an::consumer::ImageAsset>& image)
{
    ImageViewControl* preview = m_rt_preview_view_control;
    if (!image || preview == nullptr)
    {
        return false;
    }
    fitLandingPreview(*preview);
    // The preview view shares the image; it takes a document of its own again with the next result.
    an::ui::ImageDocument& shown = preview->document();
    if (!shown.valid() || !shown.isShared() || shown.asset() != image)
    {
        an::Result<an::ui::ImageDocument> shared = an::ui::ImageDocument::share(image);
        if (!shared.hasValue())
        {
            return false;
        }
        preview->setDocument(std::move(*shared));
        // A stretch taken from the target is the analysis kept for its revision and the view's presentation, if any.
        if (m_SupportExecutionAutostretch && preview->isAutoStretchEnabled() && keptAnalysisFitsPreview())
        {
            preview->notifyImageUpdatedWithPresentationAnalysis(m_impl->m_Analysis, true);
        }
    }
    else if (ImageViewWidget* widget = preview->getViewWidget(); widget != nullptr)
    {
        // The document already shares the image, but what the view paints may be an interactive frame's presentation
        // or layer, which replaced nothing in the document: the view presents the image itself again.
        widget->presentDocumentAgain();
    }
    if (m_UseHistogramPreviewWidget && m_HistogramPreview)
    {
        m_HistogramPreview->setImage(image);
    }
    clearReportedPreviewFailure();
    return true;
}

bool ImageRTPreviewControl::showKeptPreview(const RunSnapshot& snapshot)
{
    std::optional<Impl::KeptPreviews>& kept = m_impl->m_KeptPreviews;
    if (!kept.has_value())
    {
        return false;
    }
    // Only a frame over the whole target keeps its result, and only the same images, blended the same way, make it.
    const bool sameImages =
        snapshot.target && snapshot.area.isEmpty() && kept->target == snapshot.target->revision() &&
        kept->masked == (snapshot.mask != nullptr) &&
        (!snapshot.mask || (kept->mask == snapshot.mask->revision() && kept->invertMask == snapshot.invertMask)) &&
        kept->modulation == snapshot.modulation && kept->channel == snapshot.run.channel;
    // A preview stretched from the target is stretched through the target's analysis, which the view must be handed: a
    // kept result is shown only while that analysis is kept for the view's presentation, as a frame would stretch it.
    const bool stretchable = !m_SupportExecutionAutostretch || !m_rt_preview_view_control->isAutoStretchEnabled() ||
                             keptAnalysisFitsPreview();
    if (sameImages && stretchable && !snapshot.resultKey.empty())
    {
        for (const auto& [key, image] : kept->results)
        {
            if (key == snapshot.resultKey && showSharedPreview(image))
            {
                ++m_impl->m_Telemetry.keptPreviewsShown;
                return true;
            }
        }
    }
    // The frame that runs instead makes what is kept next; what was kept goes now rather than when it lands.
    kept.reset();
    return false;
}

bool ImageRTPreviewControl::keptAnalysisFitsPreview() const
{
    const ImageViewWidget* widget =
        m_rt_preview_view_control != nullptr ? m_rt_preview_view_control->getViewWidget() : nullptr;
    if (widget == nullptr || !m_impl->m_Analysis.has_value())
    {
        return false;
    }
    // Filed as a frame files the analysis it measures.
    an::ui::ViewPresentationSettings settings;
    settings.request = widget->presentationRequest();
    settings.channel = widget->displayedChannel();
    const std::shared_ptr<const an::consumer::ImageAsset> source = getExecutionAutostretchSource();
    return source && source->revision() == m_impl->m_AnalysisSource &&
           an::ui::viewPresentationKey(settings) == m_impl->m_AnalysisPolicy;
}

std::string ImageRTPreviewControl::previewResultKey(const ProcessExecutionParams*) const
{
    return {};
}

std::size_t ImageRTPreviewControl::keptPreviewCount() const
{
    return m_impl->m_KeptPreviews.has_value() ? m_impl->m_KeptPreviews->results.size() : 0u;
}

void ImageRTPreviewControl::discardKeptPreviews()
{
    m_impl->m_KeptPreviews.reset();
}

void ImageRTPreviewControl::fitPreviewBeforeItsFirstResult()
{
    ImageViewControl* preview = m_rt_preview_view_control;
    ImageViewWidget* widget = preview != nullptr ? preview->getViewWidget() : nullptr;
    if (widget == nullptr)
    {
        return;
    }
    // Owed until a first fit, and after it while no result has landed and the person kept the zoom it gave.
    const bool owed =
        m_FitToWindowRequested || (m_impl->m_FittedBeforeFirstResult.has_value() &&
                                   widget->getCurrentScaleFactor() == m_impl->m_FittedBeforeFirstResultFactor);
    // A view not shown has no viewport worth fitting to, and one that shows nothing yet - the target was being written
    // as the preview opened - has no image: either is fitted when shown or resized again, or when its first result
    // lands.
    if (!owed || !preview->isVisible() || widget->imageSize().isEmpty())
    {
        return;
    }
    preview->fitToWindow();
    m_impl->m_FittedBeforeFirstResult = widget->imageSize();
    m_impl->m_FittedBeforeFirstResultFactor = widget->getCurrentScaleFactor();
    m_FitToWindowRequested = false;
}

void ImageRTPreviewControl::fitLandingPreview(ImageViewControl& preview)
{
    if (m_FitToWindowRequested)
    {
        m_impl->m_FittedBeforeFirstResult.reset();
        m_FitToWindowRequested = false;
        preview.fitToWindowAsync();
        return;
    }
    const std::optional<QSize> fitted = std::exchange(m_impl->m_FittedBeforeFirstResult, std::nullopt);
    if (!fitted.has_value())
    {
        return;
    }
    // Decided once the result is shown, as its size is the view's only then; a zoom the person chose meanwhile on an
    // image of the same size is kept.
    QPointer<ImageViewControl> guard(&preview);
    QMetaObject::invokeMethod(
        this,
        [guard, size = *fitted]() {
            if (guard && !guard->isCloseRequested() && guard->getViewWidget() != nullptr &&
                guard->getViewWidget()->imageSize() != size)
            {
                guard->fitToWindow();
            }
        },
        Qt::QueuedConnection);
}

void ImageRTPreviewControl::watchInteraction(QSliderSpinBox& control)
{
    const QObject* held = &control;
    QObject::connect(&control, &QSliderSpinBox::interaction, &m_impl->m_ControlWatch,
                     [this, held](bool down) { controlHeld(held, down); });
    QObject::connect(&control, &QObject::destroyed, &m_impl->m_ControlWatch, [this, held]() { forgetControl(held); });
}

void ImageRTPreviewControl::watchInteraction(QSliderDoubleSpinBox& control)
{
    const QObject* held = &control;
    QObject::connect(&control, &QSliderDoubleSpinBox::interaction, &m_impl->m_ControlWatch,
                     [this, held](bool down) { controlHeld(held, down); });
    QObject::connect(&control, &QObject::destroyed, &m_impl->m_ControlWatch, [this, held]() { forgetControl(held); });
}

void ImageRTPreviewControl::controlHeld(const QObject* control, bool held)
{
    std::vector<const QObject*>& controls = m_impl->m_ControlsHeld;
    const auto found = std::find(controls.begin(), controls.end(), control);
    if (held)
    {
        if (found == controls.end())
        {
            controls.push_back(control);
        }
        return;
    }
    if (found == controls.end())
    {
        return;
    }
    controls.erase(found);
    // The drag ends with the last slider let go: the settled frame it is owed shows where it ended.
    if (controls.empty() && m_impl->m_SettledFrameOwed)
    {
        requestPreview(an::ui::PreviewFrameKind::Settled);
    }
}

void ImageRTPreviewControl::forgetControl(const QObject* control)
{
    std::vector<const QObject*>& controls = m_impl->m_ControlsHeld;
    controls.erase(std::remove(controls.begin(), controls.end(), control), controls.end());
}

void ImageRTPreviewControl::holdPreviewRequests()
{
    ++m_impl->m_PreviewHolds;
}

void ImageRTPreviewControl::releasePreviewRequests()
{
    if (m_impl->m_PreviewHolds > 0)
    {
        --m_impl->m_PreviewHolds;
    }
    if (m_impl->m_PreviewHolds == 0 && isPreviewEnabled())
    {
        requestPreview(an::ui::PreviewFrameKind::Settled);
    }
}

void ImageRTPreviewControl::reportPreviewFailure(const an::Status& status)
{
    const std::string cause = fmt::format("{}: {}", static_cast<int>(status.code()), status.message());
    if (cause == m_impl->m_ReportedFailure)
    {
        // The banner already says so; a drag that fails at every step says it once.
        return;
    }
    LOG_WARNING("Preview of process '{}' failed: {}", getProcessName(), status.message());
    showProcessMessage(QStringLiteral("The preview failed"), QString::fromStdString(status.message()),
                       ProcessMessageTone::Error);
    const QString shown = processMessage();
    if (shown.isEmpty())
    {
        // A window not shown yet has no banner: the next failure says it once there is one.
        return;
    }
    m_impl->m_ReportedFailure = cause;
    m_impl->m_ReportedFailureText = shown;
    ++m_impl->m_Telemetry.failuresReported;
}

void ImageRTPreviewControl::clearReportedPreviewFailure()
{
    if (m_impl->m_ReportedFailure.empty())
    {
        return;
    }
    // A message something else showed since is left as it is.
    if (!m_impl->m_ReportedFailureText.isEmpty() && processMessage() == m_impl->m_ReportedFailureText)
    {
        clearProcessMessage();
    }
    m_impl->m_ReportedFailure.clear();
    m_impl->m_ReportedFailureText.clear();
}

void ImageRTPreviewControl::settleSpinner()
{
    if (m_impl->m_PreviewRunning)
    {
        return;
    }
    m_impl->m_SpinnerDelay.stop();
    if (m_impl->m_SpinnerShown)
    {
        m_impl->m_SpinnerShown = false;
        controlSpinner(1);
    }
}

void ImageRTPreviewControl::releaseFrameConversions()
{
    m_impl->m_Conversions.clear();
    m_impl->m_TargetCounts.reset();
    m_impl->m_CropCounts.reset();
}

void ImageRTPreviewControl::releaseFrameCaches()
{
    // Everything frames made of the target and its mask is filed by revision, so a change finds none of it; it is let
    // go now rather than at the dialog's end, to free what a revision no frame reads again holds.
    releaseFrameConversions();
    m_impl->m_FrameProxies.clear();
    m_impl->m_Codes.reset();
    m_impl->m_CompositeBase.reset();
    m_impl->m_KeptPreviews.reset();
}

void ImageRTPreviewControl::refreshFramePeriod()
{
    if (m_impl->m_FramePeriodRefreshing || m_impl->m_Destroying || m_ProcessCloseRequested)
    {
        return;
    }
    an::ui::IAppContext* context = an::ui::appContext();
    an::ui::IGlobalSettingsProvider* settings = context != nullptr ? context->getGlobalSettings() : nullptr;
    if (settings == nullptr)
    {
        return;
    }
    // The settings store answers on a thread of its own, so the read waits there, on a worker, never on the GUI
    // thread; the window holds the read as a run in flight, so the store outlives it.
    m_impl->m_FramePeriodRefreshing = true;
    runThenPublish([settings]() { return framePeriodFrom(*settings); },
                   [this](std::chrono::nanoseconds period) {
                       m_impl->m_NextFramePeriod = period;
                       m_impl->m_FramePeriodRefreshing = false;
                       ++m_impl->m_Telemetry.frameRatesRead;
                   });
}

void ImageRTPreviewControl::runApplyInPlace(std::shared_ptr<RunSnapshot> snapshot, ProgressDialogControl* progress,
                                            bool closeAtEnd)
{
    m_ExecutionInProgress = true;
    executeProcessStarted(RTPreviewMode::NoPreview, false);
    LOG_INFO("Executing process '{}' on image '{}'... ", getProcessName(), m_image_view_control->getUniqueName());

    if (progress)
    {
        snapshot->observer = progress->observer();
        snapshot->cancel = progress->stopToken();
    }

    // What the edit's worker found, read by its completion on the GUI thread after the worker let it go.
    struct Outcome
    {
        an::Status status = an::Status::success();
    };
    auto outcome = std::make_shared<Outcome>();
    const std::shared_ptr<void> inFlight = beginRunInFlight();
    const ResultHook hook = [this](const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) {
        imageReadyToRender(result, run);
    };
    const RunBody body = [this, snapshot](const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                          const an::ui::ProcessRun& run, an::ui::ExecutionScope& scope) {
        return executeProcessImpl(source, destination, run, snapshot->params.get(), scope);
    };

    QPointer<ImageRTPreviewControl> guard(this);
    QElapsedTimer timer;
    timer.start();
    m_image_view_control->editImage(
        snapshot->step.name, snapshot->step.description,
        [snapshot, outcome, body, hook, inFlight](const an::consumer::EngineSession& session,
                                                  const an::consumer::AssetWriteLease& image,
                                                  std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
            // The edit's own stop - the document changed or closed, the view gone - and Cancel both stop the run.
            const LinkedStop runStop(stop, snapshot->cancel);
            // The edit holds the document's writer, so the source is the image under it and the mask, when it is the
            // image itself, is read through the same lease. The edit leased another view's mask and the process's own
            // images with that writer, in one order, before this run asks to be admitted.
            an::Result<MaskRead> mask = readMask(snapshot->mask, snapshot->reads.get(), image.assetIdentity(),
                                                 image.image(), image.validity(), runStop.token());
            if (!mask.hasValue())
            {
                outcome->status = mask.status();
                return mask.status();
            }
            an::Result<an::ui::ExecutionScope> scope =
                an::ui::ExecutionScope::admit(session,
                                              an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply,
                                                                       .stopToken = runStop.token(),
                                                                       .reads = snapshot->reads.get()},
                                              snapshot->observer);
            if (!scope.hasValue())
            {
                outcome->status = scope.status();
                return scope.status();
            }
            // What the run does outside its body's own guard and throws ends the run as a failure the window says.
            an::Result<an::consumer::ImageAsset> result = refusal(an::ErrorCode::Internal, "the run did not finish");
            try
            {
                result = processOneImage(session, *scope, an::ui::ProcessSource::of(image), mask->image, snapshot->run,
                                         snapshot->modulation, snapshot->invertMask, body, hook);
            }
            catch (const std::exception& error)
            {
                result = refusal(an::ErrorCode::Internal, error.what());
            }
            catch (...)
            {
                result = refusal(an::ErrorCode::Internal, "an exception of no standard type");
            }
            outcome->status = result.hasValue() ? an::Status::success() : result.status();
            scope->finish(outcome->status);
            return result;
        },
        [guard, outcome, closeAtEnd, timer](bool shown) {
            if (!guard)
            {
                return;
            }
            if (shown)
            {
                LOG_INFO("Process '{}' executed successfully on image '{}'. Execution time was {:.2f} seconds",
                         guard->getProcessName(),
                         guard->m_image_view_control ? guard->m_image_view_control->getUniqueName() : std::string(),
                         static_cast<float>(timer.elapsed()) / 1000.0f);
            }
            guard->finishRun(RTPreviewMode::NoPreview, false, outcome->status, shown, closeAtEnd);
        },
        snapshot->reads);
}

void ImageRTPreviewControl::runToNewImage(std::shared_ptr<RunSnapshot> snapshot, ProgressDialogControl* progress,
                                          const std::string& newImageName, bool closeAtEnd)
{
    const an::consumer::EngineSession* session = engineSession();
    an::Result<an::ui::CommitTicket> ticket = m_impl->m_Runs.issue(m_image_view_control->document());
    if (session == nullptr || !ticket.hasValue())
    {
        finishRun(RTPreviewMode::NoPreview, false,
                  session == nullptr ? refusal(an::ErrorCode::NotInitialized, "no engine session is lent")
                                     : ticket.status(),
                  false, closeAtEnd);
        return;
    }
    m_ExecutionInProgress = true;
    executeProcessStarted(RTPreviewMode::NoPreview, false);
    LOG_INFO("Executing process '{}' on image '{}'... ", getProcessName(), m_image_view_control->getUniqueName());

    std::shared_ptr<RunSnapshot> shared = std::move(snapshot);
    shared->ticket = std::move(*ticket);
    if (progress)
    {
        shared->observer = progress->observer();
        shared->cancel = progress->stopToken();
    }
    const ResultHook hook = [this](const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) {
        imageReadyToRender(result, run);
    };
    runThenPublish(
        [session, shared, hook, this]() {
            RunResult result;
            const RunBody body = [this, shared](const an::ui::ProcessSource& source,
                                                an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
                                                an::ui::ExecutionScope& scope) {
                return executeProcessImpl(source, destination, run, shared->params.get(), scope);
            };
            const LinkedStop runStop(shared->ticket.stopToken(), shared->cancel);
            // Every image the run reads is leased before it asks to be admitted, one lease each and in one order: an
            // edit holds its view's writer while it waits to be admitted.
            const an::Status taken = shared->reads->take(runStop.token());
            const ReadsRelease letGo(*shared->reads);
            if (taken.failed())
            {
                result.status = taken;
                return result;
            }
            an::Result<an::consumer::AssetReadLease> lease = heldShare(*shared->reads, *shared->target);
            if (!lease.hasValue())
            {
                result.status = lease.status();
                return result;
            }
            an::Result<MaskRead> mask = readMask(shared->mask, shared->reads.get(), lease->revision().assetIdentity,
                                                 lease->image(), lease->validity(), runStop.token());
            if (!mask.hasValue())
            {
                result.status = mask.status();
                return result;
            }
            an::Result<an::ui::ExecutionScope> scope =
                an::ui::ExecutionScope::admit(*session,
                                              an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply,
                                                                       .stopToken = runStop.token(),
                                                                       .reads = shared->reads.get()},
                                              shared->observer);
            if (!scope.hasValue())
            {
                result.status = scope.status();
                return result;
            }
            an::Result<an::consumer::ImageAsset> processed =
                processOneImage(*session, *scope, an::ui::ProcessSource::of(*lease), mask->image, shared->run,
                                shared->modulation, shared->invertMask, body, hook);
            if (processed.hasValue())
            {
                result.asset.emplace(std::move(*processed));
            }
            else
            {
                result.status = processed.status();
            }
            scope->finish(result.status);
            return result;
        },
        [this, shared, newImageName, closeAtEnd](RunResult result) {
            bool published = false;
            const an::ui::CommitVerdict verdict = shared->ticket.verdict();
            if (verdict == an::ui::CommitVerdict::Commit && result.status.ok() && result.asset.has_value())
            {
                an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                    std::move(*result.asset), an::ui::DocumentNames{.displayName = QString::fromUtf8(newImageName)});
                if (document.hasValue())
                {
                    if (auto* factory = process_utils::currentViewFactory())
                    {
                        published = factory->createNewImageViewControlFromImage(
                                        nullptr, std::move(*document), nullptr, shared->step.description,
                                        ViewControlMode::standard,
                                        m_CreatedViewControlsAreNeighbour ? m_image_view_control : nullptr) != nullptr;
                    }
                }
                else
                {
                    result.status = document.status();
                }
                if (published)
                {
                    LOG_INFO("Process '{}' executed successfully, created new processed image '{}'", getProcessName(),
                             newImageName);
                }
            }
            finishRun(RTPreviewMode::NoPreview, false, result.status, published, closeAtEnd);
        },
        0, resultOfWorkThatThrew);
}

void ImageRTPreviewControl::takeSnapshot(const std::string& newImageName)
{
    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr || !m_image_view_control || !m_image_view_control->hasImage())
    {
        return;
    }
    an::Result<an::ui::CommitTicket> ticket = m_impl->m_Runs.issue(m_image_view_control->document());
    if (!ticket.hasValue())
    {
        return;
    }
    std::shared_ptr<RunSnapshot> shared = snapshotRun(an::ui::ProcessRunKind::Snapshot, getProcessParamsForExecution());
    shared->ticket = std::move(*ticket);
    if (!shared->area.isEmpty() && m_impl->m_CompositeBase && m_impl->m_CompositeBaseSource == shared->ticket.source())
    {
        shared->keptBase = m_impl->m_CompositeBase;
    }
    executeProcessStarted(RTPreviewMode::TakeSnapshot, false);
    const ResultHook hook = [this](const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) {
        imageReadyToRender(result, run);
    };
    runThenPublish(
        [session, shared, hook, this]() {
            RunResult result;
            const RunBody body = [this, shared](const an::ui::ProcessSource& source,
                                                an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
                                                an::ui::ExecutionScope& scope) {
                return executeProcessImpl(source, destination, run, shared->params.get(), scope);
            };
            const std::stop_token stop = shared->ticket.stopToken();
            // Every image the snapshot reads is leased before it asks to be admitted, one lease each and in one order:
            // an edit holds its view's writer while it waits to be admitted.
            const an::Status taken = shared->reads->take(stop);
            const ReadsRelease letGo(*shared->reads);
            if (taken.failed())
            {
                result.status = taken;
                return result;
            }
            an::Result<an::consumer::AssetReadLease> lease = heldShare(*shared->reads, *shared->target);
            if (!lease.hasValue())
            {
                result.status = lease.status();
                return result;
            }
            an::Result<MaskRead> mask = readMask(shared->mask, shared->reads.get(), lease->revision().assetIdentity,
                                                 lease->image(), lease->validity(), stop);
            if (!mask.hasValue())
            {
                result.status = mask.status();
                return result;
            }
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                *session,
                an::ui::ExecutionRequest{
                    .purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop, .reads = shared->reads.get()},
                shared->observer);
            if (!scope.hasValue())
            {
                result.status = scope.status();
                return result;
            }
            if (shared->area.isEmpty())
            {
                an::Result<an::consumer::ImageAsset> processed =
                    processOneImage(*session, *scope, an::ui::ProcessSource::of(*lease), mask->image, shared->run,
                                    shared->modulation, shared->invertMask, body, hook);
                if (processed.hasValue())
                {
                    result.asset.emplace(std::move(*processed));
                }
                else
                {
                    result.status = processed.status();
                }
                scope->finish(result.status);
                return result;
            }
            // A snapshot of a preview area is the composite the preview showed: the patch over the whole image.
            an::Result<an::consumer::ImageAsset> composite = [&]() -> an::Result<an::consumer::ImageAsset> {
                an::Result<an::consumer::ImageAsset> cropped = an::ui::cropProcessRegion(
                    *session, scope->engine(), scope->execution(), lease->image(), lease->validity(), shared->area);
                if (!cropped.hasValue())
                {
                    return cropped.status();
                }
                an::Result<an::consumer::AssetReadLease> croppedLease = cropped->read(stop);
                if (!croppedLease.hasValue())
                {
                    return croppedLease.status();
                }
                std::optional<an::consumer::ImageAsset> croppedMask;
                an::consumer::AssetReadLease croppedMaskLease;
                if (mask->image != nullptr)
                {
                    an::Result<an::consumer::ImageAsset> maskCrop = an::ui::cropProcessRegion(
                        *session, scope->engine(), scope->execution(), *mask->image, mask->validity, shared->area);
                    if (!maskCrop.hasValue())
                    {
                        return maskCrop.status();
                    }
                    croppedMask.emplace(std::move(*maskCrop));
                    an::Result<an::consumer::AssetReadLease> maskLease = croppedMask->read(stop);
                    if (!maskLease.hasValue())
                    {
                        return maskLease.status();
                    }
                    croppedMaskLease = std::move(*maskLease);
                }
                an::Result<an::consumer::ImageAsset> patch =
                    processOneImage(*session, *scope, an::ui::ProcessSource::of(*croppedLease),
                                    croppedMaskLease.valid() ? &croppedMaskLease.image() : nullptr, shared->run,
                                    shared->modulation, shared->invertMask, body, hook);
                if (!patch.hasValue())
                {
                    return patch.status();
                }
                an::Result<an::consumer::AssetReadLease> patchLease = patch->read(stop);
                const std::optional<an::ImageDescriptor> patchDescribed = patch->descriptor();
                const std::optional<an::ImageDescriptor> targetDescribed = shared->target->descriptor();
                if (!patchLease.hasValue() || !patchDescribed.has_value() || !targetDescribed.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the snapshot's patch holds nothing");
                }
                const std::optional<an::ImageDescriptor> baseDescribed =
                    an::ui::processCompositeBaseDescriptor(*patchDescribed, *targetDescribed, scope->engine().limits());
                if (!baseDescribed.has_value())
                {
                    return refusal(an::ErrorCode::IncompatibleArguments,
                                   "the preview area cannot be shown over the whole image");
                }
                std::shared_ptr<const an::consumer::ImageAsset> base = shared->keptBase;
                if (!base || base->descriptor() != baseDescribed)
                {
                    an::Result<an::consumer::ImageAsset> built =
                        an::ui::buildProcessCompositeBase(*session, scope->engine(), scope->execution(), lease->image(),
                                                          lease->validity(), *baseDescribed);
                    if (!built.hasValue())
                    {
                        return built.status();
                    }
                    base = std::make_shared<const an::consumer::ImageAsset>(std::move(*built));
                }
                an::Result<an::consumer::AssetReadLease> baseLease = base->read(stop);
                if (!baseLease.hasValue())
                {
                    return baseLease.status();
                }
                return an::ui::composeProcessRegion(*session, scope->engine(), scope->execution(), baseLease->image(),
                                                    patchLease->image(), patchLease->validity(),
                                                    shared->area.topLeft());
            }();
            if (composite.hasValue())
            {
                result.asset.emplace(std::move(*composite));
            }
            else
            {
                result.status = composite.status();
            }
            scope->finish(result.status);
            return result;
        },
        [this, shared, newImageName](RunResult result) {
            bool published = false;
            if (shared->ticket.verdict() == an::ui::CommitVerdict::Commit && result.status.ok() &&
                result.asset.has_value())
            {
                an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                    std::move(*result.asset), an::ui::DocumentNames{.displayName = QString::fromUtf8(newImageName)});
                auto* factory = process_utils::currentViewFactory();
                if (document.hasValue() && factory)
                {
                    published = factory->createNewImageViewControlFromImage(
                                    nullptr, std::move(*document), nullptr, shared->step.description,
                                    ViewControlMode::standard, m_image_view_control) != nullptr;
                }
                if (published)
                {
                    ++m_impl->m_Telemetry.runsCommitted;
                    LOG_INFO("New image snapshot '{}' taken from real time preview of process '{}'", newImageName,
                             getProcessName());
                }
            }
            if (!published)
            {
                if (result.status.failed() && result.status.code() != an::ErrorCode::Cancelled)
                {
                    ++m_impl->m_Telemetry.runsFailed;
                    notifyErrorMessage("Error executing the process", describeFailure(result.status));
                }
                else
                {
                    ++m_impl->m_Telemetry.runsDiscarded;
                }
            }
            executeProcessEnded(RTPreviewMode::TakeSnapshot, false, published);
        },
        0, resultOfWorkThatThrew);
}

void ImageRTPreviewControl::runOverSequence(std::shared_ptr<RunSnapshot> snapshot, ProgressDialogControl* progress,
                                            bool createNewSequence, const std::string& newName, bool closeAtEnd)
{
    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr || !snapshot->sequence.has_value())
    {
        finishRun(RTPreviewMode::NoPreview, true,
                  refusal(an::ErrorCode::NotInitialized,
                          session == nullptr ? "no engine session is lent" : "the target states no sequence"),
                  false, closeAtEnd);
        return;
    }
    auto* context = an::ui::appContext();
    an::ui::ITempFileService* temporaryFiles = context ? context->getTempFileService() : nullptr;
    if (temporaryFiles == nullptr)
    {
        finishRun(RTPreviewMode::NoPreview, true,
                  refusal(an::ErrorCode::NotInitialized, "no temporary file service is lent for the sequence"), false,
                  closeAtEnd);
        return;
    }
    an::Result<an::ui::CommitTicket> ticket = m_impl->m_Runs.issue(m_image_view_control->document());
    if (!ticket.hasValue())
    {
        finishRun(RTPreviewMode::NoPreview, true, ticket.status(), false, closeAtEnd);
        return;
    }

    m_ExecutionInProgress = true;
    executeProcessStarted(RTPreviewMode::NoPreview, true);
    LOG_INFO("Executing process '{}' on sequence '{}'... ", getProcessName(), m_image_view_control->getUniqueName());

    std::shared_ptr<RunSnapshot> shared = std::move(snapshot);
    shared->ticket = std::move(*ticket);
    shared->outputPath =
        QString::fromStdString(temporaryFiles->createTempFilePathForNewSequence(shared->sequence->descriptor.format));
    if (progress)
    {
        shared->observer = progress->observer();
        shared->cancel = progress->stopToken();
        shared->progressDialog = progress;
    }
    // The frames the run reads are the ones the player shows; it holds still until the run lands.
    if (an::ui::IViewSequencePlayer* player = m_image_view_control->sequencePlayer())
    {
        player->setEnabled(false);
    }
    const ResultHook hook = [this](const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) {
        imageReadyToRender(result, run);
    };
    runThenPublish(
        [session, shared, hook, this]() {
            RunResult result;
            const RunBody body = [this, shared](const an::ui::ProcessSource& source,
                                                an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
                                                an::ui::ExecutionScope& scope) {
                return executeProcessImpl(source, destination, run, shared->params.get(), scope);
            };
            const LinkedStop runStop(shared->ticket.stopToken(), shared->cancel);
            const std::stop_token stop = runStop.token();
            // The mask, another view's image, and the process's own images are leased before the run asks to be
            // admitted, one lease each and in one order: an edit of a view holds its writer while it waits to be
            // admitted.
            const an::Status taken = shared->reads->take(stop);
            const ReadsRelease letGo(*shared->reads);
            if (taken.failed())
            {
                result.status = taken;
                return result;
            }
            std::optional<an::consumer::AssetReadLease> maskLease;
            const an::consumer::AnyImage* mask = nullptr;
            if (shared->mask)
            {
                an::Result<an::consumer::AssetReadLease> read = heldShare(*shared->reads, *shared->mask);
                if (!read.hasValue())
                {
                    result.status = read.status();
                    return result;
                }
                maskLease.emplace(std::move(*read));
                mask = &maskLease->image();
            }
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                *session,
                an::ui::ExecutionRequest{
                    .purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop, .reads = shared->reads.get()},
                shared->observer);
            if (!scope.hasValue())
            {
                result.status = scope.status();
                return result;
            }
            const auto run = [&]() -> an::Status {
                an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
                    an::ui::openProcessFrames(*session, *shared->sequence, shared->autoDebayer);
                if (!reader.hasValue())
                {
                    return reader.status();
                }
                an::Result<std::unique_ptr<an::ui::IProcessFrameWriter>> writer =
                    an::ui::createProcessFrameOutput(*session, *shared->sequence, shared->outputPath);
                if (!writer.hasValue())
                {
                    return writer.status();
                }
                const std::uint64_t total = (*reader)->frameCount();
                const std::uint64_t shownSource = shared->sequence->currentFrame;
                std::uint64_t written = 0u;
                std::uint64_t shownWritten = 0u;
                for (;;)
                {
                    if (stop.stop_requested())
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
                    postSequenceProgress(shared->progressDialog,
                                         total > 0u ? static_cast<int>((written * 100u) / total) : 0,
                                         QString::fromStdString(fmt::format(
                                             "Executing process in sequence. Frame {}/{}", written + 1u, total)));
                    an::Result<an::consumer::AssetReadLease> frameLease = (**frame).asset.read(stop);
                    if (!frameLease.hasValue())
                    {
                        return frameLease.status();
                    }
                    an::ui::ProcessRun frameRun = shared->run;
                    frameRun.frame = (**frame).index;
                    frameRun.frameCount = total;
                    an::Result<an::consumer::ImageAsset> processed =
                        processOneImage(*session, *scope, an::ui::ProcessSource::of(*frameLease), mask, frameRun,
                                        shared->modulation, shared->invertMask, body, hook);
                    if (!processed.hasValue())
                    {
                        return processed.status();
                    }
                    an::Result<an::consumer::AssetReadLease> processedLease = processed->read(stop);
                    if (!processedLease.hasValue())
                    {
                        return processedLease.status();
                    }
                    const an::Status appended =
                        (*writer)->write(scope->engine(), scope->execution(), (**frame).index, processedLease->image());
                    if (appended.failed())
                    {
                        return appended;
                    }
                    processedLease->release();
                    // The frame the view shows is the one it showed before, when the run processed it, and else the
                    // first.
                    if (!result.asset.has_value() || (**frame).index == shownSource)
                    {
                        result.asset.emplace(std::move(*processed));
                        shownWritten = written;
                    }
                    ++written;
                }
                if (written == 0u)
                {
                    return refusal(an::ErrorCode::EmptySelection, "the sequence held no frame to process");
                }
                an::Result<an::ui::DocumentSequence> finished = (*writer)->finish(scope->engine(), scope->execution());
                if (!finished.hasValue())
                {
                    return finished.status();
                }
                finished->currentFrame = shownWritten;
                result.sequence = std::move(*finished);
                return an::Status::success();
            };
            result.status = run();
            if (result.status.failed())
            {
                result.asset.reset();
            }
            scope->finish(result.status);
            return result;
        },
        [this, shared, createNewSequence, newName, closeAtEnd](RunResult result) {
            if (m_image_view_control)
            {
                if (an::ui::IViewSequencePlayer* player = m_image_view_control->sequencePlayer())
                {
                    player->setEnabled(true);
                }
            }
            bool published = false;
            const an::ui::CommitVerdict verdict = shared->ticket.verdict();
            if (verdict == an::ui::CommitVerdict::Commit && result.status.ok() && result.asset.has_value() &&
                result.sequence.has_value() && m_image_view_control)
            {
                an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                    std::move(*result.asset),
                    an::ui::DocumentNames{.displayName = QString::fromUtf8(newName), .sourcePath = shared->outputPath});
                if (document.hasValue())
                {
                    document->setSequence(*result.sequence);
                    if (createNewSequence)
                    {
                        if (auto* factory = process_utils::currentViewFactory())
                        {
                            published =
                                factory->createNewImageViewControlFromSequence(
                                    nullptr, std::move(*document), nullptr, shared->autoDebayer,
                                    shared->step.description, ViewControlMode::standard,
                                    m_CreatedViewControlsAreNeighbour ? m_image_view_control : nullptr) != nullptr;
                        }
                    }
                    else
                    {
                        an::ui::DocumentNames names = m_image_view_control->document().names();
                        names.sourcePath = shared->outputPath;
                        document->setNames(std::move(names));
                        m_image_view_control->setSequenceDocument(std::move(*document), shared->autoDebayer);
                        if (an::ui::IViewHistory* history = m_image_view_control->viewHistory())
                        {
                            history->registerShownState(shared->step.name, shared->step.description);
                        }
                        published = true;
                    }
                }
                else
                {
                    result.status = document.status();
                }
            }
            if (!published && result.sequence.has_value())
            {
                // The sequence was whole at its path, and nothing shows it.
                QFile::remove(shared->outputPath);
            }
            finishRun(RTPreviewMode::NoPreview, true, result.status, published, closeAtEnd);
        },
        0, resultOfWorkThatThrew);
}

void ImageRTPreviewControl::finishRun(RTPreviewMode mode, bool sequenceMode, an::Status status, bool published,
                                      bool closeAtEnd)
{
    m_ExecutionInProgress = false;
    if (ProgressDialogControl* progress = currentProgressDialog())
    {
        progress->closeAsync();
    }
    unregisterCurrentProgressDialog();

    if (published)
    {
        ++m_impl->m_Telemetry.runsCommitted;
    }
    else if (status.code() == an::ErrorCode::Cancelled)
    {
        ++m_impl->m_Telemetry.runsCancelled;
        LOG_INFO("Process '{}' cancelled by the user", getProcessName());
    }
    else if (status.failed())
    {
        ++m_impl->m_Telemetry.runsFailed;
        LOG_ERROR("{}", describeFailure(status).toStdString());
        notifyErrorMessage("Error executing the process", describeFailure(status));
    }
    else
    {
        ++m_impl->m_Telemetry.runsDiscarded;
        LOG_WARNING("Process '{}' produced a result that was not published: its image changed or closed under it",
                    getProcessName());
    }

    executeProcessEnded(mode, sequenceMode, published);

    if (published && closeAtEnd)
    {
        close();
        return;
    }
    m_SuspendBackgroundUpdates = false;
    if (published)
    {
        // The parameters go back to the process's own once the event loop turns, and one preview of the image the run
        // made follows them: the requests the reset makes, and any made before it, collapse into it rather than show
        // the parameters just applied over their own result.
        holdPreviewRequests();
        QMetaObject::invokeMethod(
            this,
            [this]() {
                resetSettings();
                releasePreviewRequests();
            },
            Qt::QueuedConnection);
        return;
    }
    if (isPreviewEnabled())
    {
        requestPreview(an::ui::PreviewFrameKind::Settled);
    }
}

void ImageRTPreviewControl::executeProcessEnded(const RTPreviewMode previewMode, const bool sequenceMode,
                                                const bool succeeded)
{
    if (succeeded)
    {
        executeProcessCompletedImpl(previewMode, sequenceMode);
    }
    executeProcessEndedImpl(previewMode, sequenceMode);
    if (previewMode == RTPreviewMode::NoPreview)
    {
        enableProcessControls(true);
        if (m_image_view_control)
        {
            m_image_view_control->releaseLockForProcessing();
        }
    }
}

an::ui::ProcessStep ImageRTPreviewControl::describeProcessStep(const ProcessExecutionParams*) const
{
    return an::ui::ProcessStep{.name = getProcessName(), .description = std::string()};
}

std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
ImageRTPreviewControl::imagesRead(const ProcessExecutionParams*) const
{
    return {};
}

an::ui::ProcessBodyCoverage ImageRTPreviewControl::processBodyCoverage(const ProcessExecutionParams*) const
{
    return an::ui::ProcessBodyCoverage::WritesPart;
}

bool ImageRTPreviewControl::isIdentity(const ProcessExecutionParams*) const
{
    return false;
}

std::uint32_t ImageRTPreviewControl::previewRegionMargin(const ProcessExecutionParams*) const
{
    return 0u;
}

an::ui::PreviewScaling ImageRTPreviewControl::previewScaling() const
{
    return an::ui::PreviewScaling::FullOnly;
}

bool ImageRTPreviewControl::mapsEachChannelAlone() const
{
    return false;
}

bool ImageRTPreviewControl::bodyAdmitsWorkOfItsOwn() const
{
    return false;
}

void ImageRTPreviewControl::settingsUpdated()
{
    if (isPreviewEnabled())
    {
        schedulePreviewProcessExecution();
    }

    settingsUpdatedImpl();
}

void ImageRTPreviewControl::updateNeeded()
{
    settingsUpdated();
}

void ImageRTPreviewControl::createPreviewControl()
{
    if (m_rt_preview_view_control || !m_image_view_control)
    {
        return;
    }

    if (requiresExclusivePreviewAccess())
    {
        m_image_view_control->disableExclusiveRTPreviews(this);
    }

    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr || !m_image_view_control->hasImage())
    {
        notifyErrorMessage("Create Preview", "Could NOT create the preview for this process: there is no image to "
                                             "preview");
        return;
    }

    // The preview shows the target's own image until its first frame lands: shared, never copied, and never written
    // through the preview's document.
    std::optional<an::ui::ImageDocument> startingDocument;
    if (an::Result<an::ui::ImageDocument> shared =
            an::ui::ImageDocument::share(m_image_view_control->document().asset());
        shared.hasValue())
    {
        startingDocument.emplace(std::move(*shared));
    }

    auto* previewControl = new ImageViewControl(this);
    previewControl->initialize(getWindowTitle(), ViewControlMode::rt_preview);
    if (startingDocument.has_value())
    {
        previewControl->setDocument(std::move(*startingDocument));
    }
    previewControl->displayDefaultViewChannel();
    previewControl->markPendingChanges();
    m_rt_preview_view_control = previewControl;
    m_rt_preview_view_control->setUniqueName(getWindowTitle());

    m_rt_preview_view_control->subscribeListener(this);

    attachToViewWidget(m_rt_preview_view_control->getViewWidget());

    // Dont notify the view widget yet, we are still creating the view control!
    updateAlternateImage(false);

    m_rt_preview_view_control->setRTPreviewAreaActionEnabled(m_impl->m_PreviewAreaSelectionEnabled);

    if (m_impl->m_PreviewAreaSelectionEnabled)
    {
        if (QCheckBox* previewAreaCheckBox = m_rt_preview_view_control->getRTPreviewAreaCheckBox())
        {
            previewAreaCheckBox->blockSignals(true);
            previewAreaCheckBox->setChecked(m_rt_preview_use_sub_area);
            previewAreaCheckBox->blockSignals(false);

            QObject::connect(previewAreaCheckBox, &QCheckBox::toggled, this,
                             &ImageRTPreviewControl::on_rt_preview_area_toggled);
        }
    }

    if (m_rt_preview_use_sub_area)
    {
        ensurePreviewSubAreaInitialized();
        recreatePreviewSubAreaController();
    }
    else
    {
        clearPreviewSubAreaOverlay();
    }

    if (m_SupportExecutionAutostretch)
    {
        m_rt_preview_view_control->enableRTPreviewAutostretchControls(true);

        // Pre-set autostretch settings so the first AUTOSTRETCH render matches the target view control.
        if (auto* viewWidget = m_rt_preview_view_control->getViewWidget())
        {
            viewWidget->enableAutostretchStatisticsCaching(true);
            invalidateExecutionAutostretchStatistics();
            viewWidget->setAutostretchSettings(m_image_view_control->getAutostretchSettings(), false);
        }

        m_rt_preview_view_control->setViewMode(m_image_view_control->isAutoStretchEnabled()
                                                   ? ImageViewMode::AUTOSTRETCH_MODE
                                                   : ImageViewMode::LINEAR_MODE);
    }

    if (m_ImagePreviewLayout)
    {
        m_ImagePreviewLayout->addWidget(m_rt_preview_view_control);
        m_rt_preview_view_control->show();
        resize(m_DefaultSizePreview);
    }

    if (m_MainHorizontalLayout)
    {
        if (m_FixedLayout)
        {
            m_MainHorizontalLayout->setStretch(0, 0);
            m_MainHorizontalLayout->setStretch(1, 1);
        }
        else
        {
            m_MainHorizontalLayout->setStretch(0, 1);
            m_MainHorizontalLayout->setStretch(1, 3);
        }
    }

    m_ImagePreviewSpinner =
        std::make_unique<WaitingSpinnerWidget>(m_rt_preview_view_control->getScrollArea(), true, false);
    m_ImagePreviewSpinner->setColor(an::ui::tokens().colors.accentPrimary);

    // The window may be shown, laid out and resized after this - an extension's window is - so the view is fitted
    // again each time its viewport is, until the first result lands; once queued, so a burst of resizes fits once.
    m_impl->m_PreviewGeometryWatch = std::make_unique<GeometryWatch>([this]() {
        if (m_impl->m_PreviewFitQueued)
        {
            return;
        }
        m_impl->m_PreviewFitQueued = true;
        QTimer::singleShot(0, this, [this]() {
            m_impl->m_PreviewFitQueued = false;
            if (!m_impl->m_StartupPreviewPending && !m_ProcessCloseRequested)
            {
                fitPreviewBeforeItsFirstResult();
            }
        });
    });
    m_rt_preview_view_control->getScrollArea()->viewport()->installEventFilter(m_impl->m_PreviewGeometryWatch.get());

    // The dialog claims the session's preview reservation while its preview is shown.
    if (an::Result<an::consumer::PreviewReservation> reservation = session->reservePreview(); reservation.hasValue())
    {
        m_impl->m_PreviewReservation = std::move(*reservation);
    }
    // The frames are paced at the rate the settings hold, read off the GUI thread now and again as chains begin.
    refreshFramePeriod();

    m_FitToWindowRequested = true;

    previewControlStarted();

    if (m_ImagePreviewLayout)
    {
        m_ImagePreviewLayout->activate();
    }
    if (auto* lay = layout())
    {
        lay->activate();
    }

    // The view is not fitted here: the viewport's geometry is not final yet, a maximised window's above all. It is
    // fitted to the target's image once the layout has settled, so it does not wait for a first result that may take
    // seconds zoomed in; a window not shown by then leaves the fit to its first result.
    m_impl->m_StartupPreviewPending = true;

    QTimer::singleShot(0, this, [this]() {
        m_impl->m_StartupPreviewPending = false;

        if (!m_rt_preview_view_control || !isPreviewEnabled() || m_ProcessCloseRequested)
        {
            return;
        }

        fitPreviewBeforeItsFirstResult();
        requestPreview(an::ui::PreviewFrameKind::Settled);
    });
}

void ImageRTPreviewControl::destroyPreviewControl(bool processEnding)
{
    m_impl->m_StartupPreviewPending = false;
    m_impl->m_FittedBeforeFirstResult.reset();
    m_impl->m_PreviewGeometryWatch.reset();
    invalidateExecutionAutostretchStatistics();
    m_impl->m_CompositeBase.reset();
    m_impl->m_FrameProxies.clear();
    m_impl->m_Codes.reset();
    m_impl->m_KeptPreviews.reset();
    releaseFrameConversions();
    m_impl->m_PreviewReservation.release();
    // Nothing is owed to a preview that is gone, and what the banner said of its failures no longer holds.
    m_impl->m_WantedFrame.reset();
    m_impl->m_SettledFrameOwed = false;
    m_impl->m_SettleTimer.stop();
    m_impl->m_SpinnerDelay.stop();
    m_impl->m_SpinnerShown = false;
    if (!processEnding)
    {
        clearReportedPreviewFailure();
    }

    if (m_rt_preview_view_control)
    {
        destroyPreviewSubAreaController();
        clearPreviewSubAreaOverlay();

        detachFromViewWidget();
        m_rt_preview_view_control->unsubscribeListener(this);

        if (m_ImagePreviewLayout)
        {
            m_ImagePreviewLayout->removeWidget(m_rt_preview_view_control);
        }

        m_rt_preview_view_control->deepClose();
        m_rt_preview_view_control = nullptr;

        if (m_MainHorizontalLayout)
        {
            m_MainHorizontalLayout->setStretch(0, 1);
            m_MainHorizontalLayout->setStretch(1, 0);
        }

        if (!processEnding)
        {
            adjustSize();
            resize(m_DefaultSizeNoPreview);
        }

        m_ImagePreviewSpinner.reset();

        previewControlClosed();
    }

    if (m_UseHistogramPreviewWidget && m_HistogramPreview && m_image_view_control && !processEnding)
    {
        m_HistogramPreview->setImage(m_image_view_control->document().asset());
    }
}

std::shared_ptr<const an::consumer::ImageAsset> ImageRTPreviewControl::getAlternateAsset() const
{
    return m_image_view_control ? m_image_view_control->document().asset() : nullptr;
}

std::shared_ptr<const an::consumer::ImageAsset> ImageRTPreviewControl::getExecutionAutostretchSource() const
{
    return m_image_view_control ? m_image_view_control->document().asset() : nullptr;
}

void ImageRTPreviewControl::updateAlternateImage(bool notifyImageViewWidget)
{
    if (!m_rt_preview_view_control)
    {
        return;
    }
    std::shared_ptr<const an::consumer::ImageAsset> alternate = getAlternateAsset();
    if (!alternate)
    {
        LOG_WARNING("updateAlternateImage: no alternate image available");
        return;
    }
    // The original the person compares with is the target's own asset, shared rather than copied.
    m_rt_preview_view_control->setAlternateAsset(std::move(alternate), notifyImageViewWidget);
}

void ImageRTPreviewControl::invalidateExecutionAutostretchStatistics()
{
    m_impl->m_Analysis.reset();
    m_impl->m_AnalysisPolicy.clear();
}

void ImageRTPreviewControl::setupPreviewControl(const bool useSubArea, const QSize& subAreaSize,
                                                const QString& subAreaText)
{
    m_rt_preview_use_sub_area = m_impl->m_PreviewAreaSelectionEnabled && useSubArea;

    if (subAreaSize.width() > 0 && subAreaSize.height() > 0)
    {
        m_InitiaSubAreaSize = subAreaSize;
    }

    if (!subAreaText.isEmpty())
    {
        m_SubAreaText = subAreaText;
    }

    if (m_SubAreaText.isEmpty())
    {
        m_SubAreaText = QStringLiteral("Preview area");
    }

    if (m_rt_preview_use_sub_area)
    {
        resetPreviewSubAreaToDefault();
    }

    if (isPreviewEnabled())
    {
        createPreviewControl();
    }

    initializeTopLevelProcess();
}

void ImageRTPreviewControl::setupHistogramPreview(QScrollArea* _histogramPreviewScrollArea, QComboBox* _plotResolution,
                                                  QAbstractButton* zoomIn, QAbstractButton* zoomOut,
                                                  QAbstractButton* zoomReset, QAbstractButton* gridButton,
                                                  QAbstractButton* localChannelNormalization,
                                                  QAbstractButton* showClippingInfo, QAbstractButton* showHistogram,
                                                  QAbstractButton* showHistogramAreas)
{
    m_HistogramPreviewScrollArea = _histogramPreviewScrollArea;
    m_PlotResolution = _plotResolution;
    m_ZoomIn = zoomIn;
    m_ZoomOut = zoomOut;
    m_ZoomReset = zoomReset;
    m_GridButton = gridButton;
    m_LocalChannelNormalization = localChannelNormalization;
    m_ShowClippingInfo = showClippingInfo;
    m_ShowHistogram = showHistogram;
    m_ShowHistogramAreas = showHistogramAreas;

    // The histogram widgets make the histogram, set it as the scroll area's widget and count off the GUI thread. It is
    // counted again with every preview shown, so it is admitted as part of the preview.
    m_HistogramPreview =
        an::ui::createPreviewHistogram(m_HistogramPreviewScrollArea, an::consumer::OperationPurpose::Preview);
    if (m_HistogramPreview)
    {
        m_HistogramPreview->setClippingInfoEnabled(true);
    }

    const auto iconProvider = currentIconProvider();
    if (iconProvider)
    {
        m_ZoomIn->setIcon(iconProvider->getIcon("zoom_in"));
        m_ZoomOut->setIcon(iconProvider->getIcon("zoom_out"));
        m_ZoomReset->setIcon(iconProvider->getIcon("zoom_fit"));
        m_GridButton->setIcon(iconProvider->getIcon("grid"));
        m_LocalChannelNormalization->setIcon(iconProvider->getIcon("scale"));

        const QSize infoIconSize = m_ShowClippingInfo ? m_ShowClippingInfo->iconSize() : QSize(16, 16);
        m_ShowClippingInfo->setIcon(QIcon(iconProvider->loadSvgFileAsPixmap(
            QStringLiteral(":/AfterNight/icons/SVG/info.svg"), infoIconSize, iconProvider->getColorNormal())));

        if (m_ShowHistogram)
        {
            m_ShowHistogram->setIcon(iconProvider->getIcon("histogram"));
        }

        if (m_ShowHistogramAreas)
        {
            m_ShowHistogramAreas->setIcon(iconProvider->getIcon("area_chart"));
        }
    }

    if (m_ShowHistogram)
    {
        QObject::connect(m_ShowHistogram, &QAbstractButton::clicked, this, &ImageRTPreviewControl::on_rt_showHistogram);
    }

    if (m_ShowHistogramAreas)
    {
        QObject::connect(m_ShowHistogramAreas, &QAbstractButton::clicked, this,
                         &ImageRTPreviewControl::on_rt_showHistogramAreas);
    }

    updateSupportedPlotResolutions();
    m_PlotResolution->setCurrentText(PLOT_RESOLUTION_8BIT);

    QObject::connect(m_LocalChannelNormalization, &QAbstractButton::clicked, this,
                     &ImageRTPreviewControl::on_rt_localChannelNormalization);
    QObject::connect(m_ShowClippingInfo, &QAbstractButton::clicked, this,
                     &ImageRTPreviewControl::on_rt_showClippingInfo);
    QObject::connect(m_GridButton, &QAbstractButton::clicked, this, &ImageRTPreviewControl::on_rt_gridClicked);
    QObject::connect(m_PlotResolution, &QComboBox::currentTextChanged, this,
                     &ImageRTPreviewControl::on_rt_plotResolution_currentTextChanged);
    QObject::connect(m_ZoomIn, &QAbstractButton::clicked, this, &ImageRTPreviewControl::on_rt_zoomInButton);
    QObject::connect(m_ZoomOut, &QAbstractButton::clicked, this, &ImageRTPreviewControl::on_rt_zoomOutButton);
    QObject::connect(m_ZoomReset, &QAbstractButton::clicked, this, &ImageRTPreviewControl::on_rt_zoomResetButton);

    m_UseHistogramPreviewWidget = true;
}

void ImageRTPreviewControl::onContentLayoutReplaced(QBoxLayout* contentLayout)
{
    // The modern-shell wrapper deleted the original top-level layout and
    // moved its children into contentLayout.  Update our cached pointer so
    // that createPreviewControl / destroyPreviewControl operate on the
    // correct (live) layout instead of the now-deleted one.
    m_MainHorizontalLayout = contentLayout;

    if (m_MainHorizontalLayout)
    {
        m_MainHorizontalLayout->setSpacing(kProcessMainPanelsSpacing);

        if (auto* splitter = an::ui::findMainProcessSplitter(m_MainHorizontalLayout))
        {
            const bool previewActive = (m_rt_preview_view_control != nullptr);
            an::ui::setPreviewPaneVisible(splitter, previewActive);

            if (!previewActive)
            {
                an::ui::collapsePreviewPane(splitter);
            }
        }

        if (auto* previewPanel = an::ui::findPreviewPanelWidget(m_ImagePreviewLayout))
        {
            if (m_rt_preview_view_control)
            {
                previewPanel->setMinimumWidth(kProcessMinPreviewPaneWidth);
                previewPanel->setMaximumWidth(QWIDGETSIZE_MAX);
                previewPanel->setVisible(true);
            }
            else
            {
                previewPanel->setVisible(false);
                previewPanel->setMaximumWidth(0);
                previewPanel->setMinimumWidth(0);
            }
        }
    }
}

void ImageRTPreviewControl::enableProcessControls(bool enabled)
{
    ImageProcessControl::enableProcessControls(enabled);
    if (m_rt_preview_view_control)
    {
        m_rt_preview_view_control->setRTPreviewTakeSnapshotActionEnabled(enabled);
        m_rt_preview_view_control->setRTPreviewAreaActionEnabled(enabled && m_impl->m_PreviewAreaSelectionEnabled);
    }
}

void ImageRTPreviewControl::setPreviewAreaSelectionEnabled(bool enabled)
{
    m_impl->m_PreviewAreaSelectionEnabled = enabled;
    if (!enabled && m_rt_preview_use_sub_area)
    {
        m_rt_preview_use_sub_area = false;
        destroyPreviewSubAreaController();
        clearPreviewSubAreaOverlay();
    }
    if (!m_rt_preview_view_control)
    {
        return;
    }
    m_rt_preview_view_control->setRTPreviewAreaActionEnabled(enabled);
    if (QCheckBox* previewAreaCheckBox = m_rt_preview_view_control->getRTPreviewAreaCheckBox())
    {
        // The view's checkbox drives the area only while the person may draw one.
        QObject::disconnect(previewAreaCheckBox, &QCheckBox::toggled, this,
                            &ImageRTPreviewControl::on_rt_preview_area_toggled);
        previewAreaCheckBox->blockSignals(true);
        previewAreaCheckBox->setChecked(m_rt_preview_use_sub_area);
        previewAreaCheckBox->blockSignals(false);
        if (enabled)
        {
            QObject::connect(previewAreaCheckBox, &QCheckBox::toggled, this,
                             &ImageRTPreviewControl::on_rt_preview_area_toggled);
        }
    }
}

ImageViewControl* ImageRTPreviewControl::previewView() const
{
    return m_rt_preview_view_control;
}

QRect ImageRTPreviewControl::previewArea() const
{
    return m_rt_preview_use_sub_area ? normalizedPreviewSubArea(m_rt_preview_sub_area) : QRect();
}

ProcessRunTelemetry ImageRTPreviewControl::runTelemetry() const
{
    return m_impl->m_Telemetry;
}

bool ImageRTPreviewControl::isProcessIdle() const
{
    // A drag held still is not idle until the settled frame it is owed has been asked for.
    const bool settled = !(m_impl->m_SettledFrameOwed && m_impl->m_SettleTimer.isActive());
    const bool previewIdle = !m_impl->m_PreviewRunning && !m_impl->m_WantedFrame.has_value() &&
                             !m_impl->m_PreviewTimerArmed && !m_impl->m_StartupPreviewPending &&
                             m_impl->m_PreviewHolds == 0 && settled;
    const bool viewIdle = !m_rt_preview_view_control || m_rt_preview_view_control->isWorkIdle();
    return previewIdle && viewIdle && isRunIdle() && !m_ExecutionInProgress;
}

void ImageRTPreviewControl::updateSupportedPlotResolutions()
{
    if (!m_PlotResolution)
        return;

    m_PlotResolution->clear();

    m_PlotResolution->addItem(PLOT_RESOLUTION_6BIT);
    m_PlotResolution->addItem(PLOT_RESOLUTION_7BIT);
    m_PlotResolution->addItem(PLOT_RESOLUTION_8BIT);
    m_PlotResolution->addItem(PLOT_RESOLUTION_9BIT);
    m_PlotResolution->addItem(PLOT_RESOLUTION_10BIT);
    m_PlotResolution->addItem(PLOT_RESOLUTION_12BIT);
    m_PlotResolution->addItem(PLOT_RESOLUTION_14BIT);
    m_PlotResolution->addItem(PLOT_RESOLUTION_16BIT);
    m_PlotResolution->addItem(PLOT_RESOLUTION_100_LEVELS);
    m_PlotResolution->addItem(PLOT_RESOLUTION_1000_LEVELS);
    m_PlotResolution->addItem(PLOT_RESOLUTION_10000_LEVELS);
}

void ImageRTPreviewControl::controlSpinner(int op)
{
    if (!m_ImagePreviewSpinner)
    {
        return;
    }

    if (op == 0)
    {
        m_ImagePreviewSpinner->start();
    }
    else
    {
        m_ImagePreviewSpinner->stop();
    }
}

void ImageRTPreviewControl::controlSpinnerAsync(int op)
{
    QMetaObject::invokeMethod(this, [this, op]() { controlSpinner(op); }, Qt::QueuedConnection);
}

void ImageRTPreviewControl::refreshTheme()
{
    ImageProcessControl::refreshTheme();

    // Refresh the RT preview view control if it exists
    if (m_rt_preview_view_control)
    {
        m_rt_preview_view_control->refreshTheme();
    }

    // Refresh histogram preview button icons if they exist
    if (m_UseHistogramPreviewWidget)
    {
        const auto iconProvider = currentIconProvider();
        if (!iconProvider)
        {
            return;
        }

        if (m_ZoomIn)
        {
            m_ZoomIn->setIcon(iconProvider->getIcon("zoom_in"));
        }
        if (m_ZoomOut)
        {
            m_ZoomOut->setIcon(iconProvider->getIcon("zoom_out"));
        }
        if (m_ZoomReset)
        {
            m_ZoomReset->setIcon(iconProvider->getIcon("zoom_fit"));
        }
        if (m_GridButton)
        {
            m_GridButton->setIcon(iconProvider->getIcon("grid"));
        }
        if (m_LocalChannelNormalization)
        {
            m_LocalChannelNormalization->setIcon(iconProvider->getIcon("scale"));
        }
        if (m_ShowClippingInfo)
        {
            m_ShowClippingInfo->setIcon(QIcon(
                iconProvider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/info.svg"),
                                                  m_ShowClippingInfo->iconSize(), iconProvider->getColorNormal())));
        }
        if (m_ShowHistogram)
        {
            m_ShowHistogram->setIcon(iconProvider->getIcon("histogram"));
        }
        if (m_ShowHistogramAreas)
        {
            m_ShowHistogramAreas->setIcon(iconProvider->getIcon("area_chart"));
        }
    }
}

void ImageRTPreviewControl::recreatePreviewSubAreaController()
{
    if (!m_rt_preview_view_control || !m_rt_preview_use_sub_area)
    {
        return;
    }

    ensurePreviewSubAreaInitialized();
    destroyPreviewSubAreaController();

    m_CropController = std::make_unique<ImageCropController>(m_rt_preview_view_control->getScrollArea(), this);
    m_CropController->setListenerEnabled(true);
    m_CropController->enableOneClickMove(true);

    updatePreviewSubAreaOverlay();
}

void ImageRTPreviewControl::destroyPreviewSubAreaController()
{
    if (!m_CropController)
    {
        return;
    }

    m_CropController->destroy();
    m_CropController.reset();
}

void ImageRTPreviewControl::resetPreviewSubAreaToDefault()
{
    m_rt_preview_sub_area = an::ui::centeredPreviewArea(sourceImageBounds(m_image_view_control), m_InitiaSubAreaSize);
}

void ImageRTPreviewControl::ensurePreviewSubAreaInitialized()
{
    m_rt_preview_sub_area = normalizedPreviewSubArea(m_rt_preview_sub_area);
    if (m_rt_preview_sub_area.isEmpty())
    {
        resetPreviewSubAreaToDefault();
    }
}

void ImageRTPreviewControl::updatePreviewSubAreaOverlay()
{
    if (!m_rt_preview_view_control || !m_rt_preview_use_sub_area)
    {
        return;
    }

    ensurePreviewSubAreaInitialized();

    auto* viewWidget = m_rt_preview_view_control->getViewWidget();
    if (!viewWidget)
    {
        return;
    }

    viewWidget->setSelectionAreaText(m_SubAreaText);
    viewWidget->setImageViewSelectionStyle(ImageViewSelectionStyle::PREVIEW_STYLE);
    viewWidget->updateSelectionArea(m_rt_preview_sub_area);
    viewWidget->enableSelectionArea(true);
    viewWidget->update();
}

void ImageRTPreviewControl::clearPreviewSubAreaOverlay()
{
    if (!m_rt_preview_view_control)
    {
        return;
    }

    auto* viewWidget = m_rt_preview_view_control->getViewWidget();
    if (!viewWidget)
    {
        return;
    }

    viewWidget->clearSelectionAreaText();
    viewWidget->enableSelectionArea(false);
    viewWidget->update();
}

QRect ImageRTPreviewControl::normalizedPreviewSubArea(const QRect& area) const
{
    const QRect imageBounds = sourceImageBounds(m_image_view_control);
    if (imageBounds.isEmpty())
    {
        return {};
    }

    return area.normalized().intersected(imageBounds);
}
