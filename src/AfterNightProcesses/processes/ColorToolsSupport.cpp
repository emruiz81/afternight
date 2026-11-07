// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour calibration tools' snapshots, reference reads, measurements and applies.

#include "stdafx.hpp"

#include "processes/ColorToolsSupport.h"

#include "controls/ImageViewControl.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PhotometricCalibrationSupport.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/StretchProcessSupport.h"
#include "session/ImageDocument.h"
#include "session/ImageReads.h"

#include <AfterNightEngine/Algorithms/Photometry/ColorCalibrationApply.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <variant>

namespace an::ui::processes
{

namespace
{

[[nodiscard]] an::Status refusal(const an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)});
}

template <typename ImageT> using SampleOf = typename std::remove_cvref_t<ImageT>::Sample;

/// A channel's name in a message: red, green and blue for a colour image's three.
[[nodiscard]] std::string channelName(const std::uint32_t channel, const std::uint32_t channels)
{
    if (channels == 3u && channel < 3u)
    {
        static constexpr std::array<const char*, 3u> kNames{"red", "green", "blue"};
        return kNames[channel];
    }
    return fmt::format("channel {}", channel + 1u);
}

/// The rule a packed mask decides.
[[nodiscard]] an::ValidityDescriptor explicitMaskRule()
{
    return an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask})
        .value_or(an::ValidityDescriptor{});
}

/// A new mask over an image's width and height, of `planes` bit-planes.
[[nodiscard]] an::Result<an::ValidityMask> newMask(const an::EngineContext& engine, const std::uint32_t width,
                                                   const std::uint32_t height, const std::uint32_t planes)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, planes, engine.limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the mask cannot be described");
    }
    return an::ValidityMask::create(engine, *extent);
}

/// The validity a working copy of a source - its samples as fractions of full scale - is read under: the source's own
/// rule when the rule means the same of a fraction as of the stored sample - every sample, the finite ones, the
/// source's own mask - and otherwise the source's rule judged on its stored samples once and carried as a mask shared
/// by every channel, valid where every channel's sample is. Not copied or moved: its view points into it.
class WorkingValidity
{
public:
    /// The validity of a source's working copy.
    ///
    /// @param engine the engine context a judged mask is taken from.
    /// @param source the source.
    /// @return the validity; or what judging the rule refused.
    [[nodiscard]] static an::Result<std::unique_ptr<WorkingValidity>> of(const an::EngineContext& engine,
                                                                         const an::ui::ProcessSource& source)
    {
        std::unique_ptr<WorkingValidity> validity(new WorkingValidity());
        const an::ValidityDescriptor rule = source.metadata().semantics().validity();
        const an::ValidityMask* mask = source.validity();
        if (rule.rule() == an::ValidityRule::ExplicitMask && mask != nullptr)
        {
            validity->m_maskView = mask->view();
            if (!validity->m_maskView.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the image's mask holds nothing");
            }
            validity->m_view = an::KernelValidityView{.descriptor = rule, .mask = &*validity->m_maskView};
            return validity;
        }
        if (rule.rule() == an::ValidityRule::AllValid || rule.rule() == an::ValidityRule::FiniteOnly)
        {
            validity->m_view = an::KernelValidityView{.descriptor = rule};
            return validity;
        }
        const std::optional<an::ImageDescriptor> described = source.descriptor();
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
        }
        an::Result<an::ValidityMask> judged =
            newMask(engine, described->extent().width(), described->extent().height(), 1u);
        if (!judged.hasValue())
        {
            return judged.status();
        }
        std::optional<an::ValidityMaskView<std::uint8_t>> bits = judged->view();
        if (!bits.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the judged mask holds nothing");
        }
        const an::Status marked = std::visit(
            [&](const auto& typed) -> an::Status {
                const auto view = typed.view();
                if (!view.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
                }
                return an::markValidity(*bits, *view, rule);
            },
            source.image());
        if (marked.failed())
        {
            return marked;
        }
        validity->m_judged.emplace(std::move(*judged));
        validity->m_maskView = validity->m_judged->view();
        validity->m_view = an::KernelValidityView{.descriptor = explicitMaskRule(), .mask = &*validity->m_maskView};
        return validity;
    }

    WorkingValidity(const WorkingValidity&) = delete;
    WorkingValidity& operator=(const WorkingValidity&) = delete;
    WorkingValidity(WorkingValidity&&) = delete;
    WorkingValidity& operator=(WorkingValidity&&) = delete;
    ~WorkingValidity() = default;

    /// What an engine call reads the working copy under.
    [[nodiscard]] const an::KernelValidityView& view() const noexcept
    {
        return m_view;
    }

    /// Whether a finite working sample means something: what the mask says, and always under the two rules kept as
    /// rules - every sample, and every finite one.
    [[nodiscard]] bool admits(const std::uint32_t channel, const std::uint32_t x, const std::uint32_t y) const noexcept
    {
        if (m_view.mask != nullptr)
        {
            const std::uint32_t plane = m_view.mask->isShared() ? 0u : channel;
            return m_view.mask->test(plane, x, y).value_or(false);
        }
        return true;
    }

private:
    WorkingValidity() = default;

    std::optional<an::ValidityMask> m_judged;
    std::optional<an::ValidityMaskView<const std::uint8_t>> m_maskView;
    an::KernelValidityView m_view{};
};

/// Which samples of a working copy a measurement keeps, a bit-plane per channel: those its validity admits that are
/// finite, within the range when one is given and, when asked, not zero.
template <typename WorkT>
[[nodiscard]] an::Result<an::ValidityMask>
keptSamples(const an::EngineContext& engine, const an::ImageView<const WorkT>& view, const WorkingValidity& validity,
            const std::optional<FractionRange>& range, const bool zeroMeansNothing)
{
    an::Result<an::ValidityMask> kept = newMask(engine, view.width(), view.height(), view.channelCount());
    if (!kept.hasValue())
    {
        return kept.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> bits = kept->view();
    if (!bits.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the kept samples' mask holds nothing");
    }
    const std::uint32_t perPixel = view.samplesPerPixel();
    for (std::uint32_t channel = 0u; channel < view.channelCount(); ++channel)
    {
        const std::uint32_t plane = channel / perPixel;
        const std::uint32_t within = channel % perPixel;
        for (std::uint32_t y = 0u; y < view.height(); ++y)
        {
            const WorkT* sample = view.sampleAt(plane, 0u, y, within);
            for (std::uint32_t x = 0u; x < view.width(); ++x, sample += perPixel)
            {
                const double value = static_cast<double>(*sample);
                bool keep = std::isfinite(value) && validity.admits(channel, x, y);
                if (keep && range.has_value())
                {
                    keep = value >= range->lowest && value <= range->highest;
                }
                if (keep && zeroMeansNothing)
                {
                    keep = value != 0.0;
                }
                (void)bits->setValid(channel, x, y, keep);
            }
        }
    }
    return kept;
}

/// A source's working copy and which of its samples a measurement keeps.
struct KeptCopy
{
    an::consumer::AnyImage working;
    an::ValidityMask kept;
};

[[nodiscard]] an::Result<KeptCopy> keptCopyOf(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                                              const std::optional<FractionRange>& range, const bool zeroMeansNothing)
{
    an::Result<an::consumer::AnyImage> working =
        makeStretchWorkingCopy(scope.engine(), scope.execution(), source.image());
    if (!working.hasValue())
    {
        return working.status();
    }
    an::Result<std::unique_ptr<WorkingValidity>> validity = WorkingValidity::of(scope.engine(), source);
    if (!validity.hasValue())
    {
        return validity.status();
    }
    an::Result<an::ValidityMask> kept = std::visit(
        [&](const auto& typed) -> an::Result<an::ValidityMask> {
            using WorkT = SampleOf<decltype(typed)>;
            if constexpr (!std::is_floating_point_v<WorkT>)
            {
                return refusal(an::ErrorCode::Internal, "a working copy holds floating samples");
            }
            else
            {
                const auto view = typed.view();
                if (!view.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
                }
                return keptSamples<WorkT>(scope.engine(), *view, **validity, range, zeroMeansNothing);
            }
        },
        *working);
    if (!kept.hasValue())
    {
        return kept.status();
    }
    return KeptCopy{.working = std::move(*working), .kept = std::move(*kept)};
}

/// Which statistic a measurement takes of each channel.
enum class Statistic : std::uint8_t
{
    Median,
    Mean
};

[[nodiscard]] an::Result<an::ColorChannelValues> measureChannels(const an::ui::ExecutionScope& scope,
                                                                 const an::ui::ProcessSource& reference,
                                                                 const std::optional<an::StatisticsRegion>& region,
                                                                 const std::optional<FractionRange>& range,
                                                                 const std::string_view what, const Statistic statistic)
{
    const std::optional<an::ImageDescriptor> described = reference.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, fmt::format("the {} holds nothing", what));
    }
    if (!isCalibrationRgb(*described))
    {
        return refusal(an::ErrorCode::Unsupported, fmt::format("the {} is not a three-channel colour image", what));
    }
    an::Result<KeptCopy> copy = keptCopyOf(scope, reference, range, false);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> kept = std::as_const(copy->kept).view();
    if (!kept.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the kept samples' mask holds nothing");
    }
    const an::StatisticsSelection selection{.validity = explicitMaskRule(), .mask = &*kept, .region = region};
    return std::visit(
        [&](const auto& typed) -> an::Result<an::ColorChannelValues> {
            using WorkT = SampleOf<decltype(typed)>;
            const auto view = typed.view();
            if (!view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
            }
            an::ColorChannelValues values{};
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                an::Status measured = an::Status::success();
                if (statistic == Statistic::Median)
                {
                    const an::Result<an::RobustStatistics> robust = an::computeImageRobustStatistics(
                        scope.execution(), an::kSampleTypeOf<WorkT>, view->data(), view->layout(), channel, selection,
                        an::RobustStatisticsRequest{});
                    if (robust.hasValue())
                    {
                        values[channel] = robust->median;
                    }
                    measured = robust.hasValue() ? an::Status::success() : robust.status();
                }
                else
                {
                    const an::Result<an::BasicStatistics> basic = an::computeImageBasicStatistics(
                        scope.execution(), an::kSampleTypeOf<WorkT>, view->data(), view->layout(), channel, selection);
                    if (basic.hasValue())
                    {
                        values[channel] = basic->mean;
                    }
                    measured = basic.hasValue() ? an::Status::success() : basic.status();
                }
                if (measured.code() == an::ErrorCode::EmptySelection)
                {
                    return refusal(an::ErrorCode::EmptySelection,
                                   range.has_value()
                                       ? fmt::format("No {} sample of the {} lies between the limits {} and {}; widen "
                                                     "the limits or choose another reference",
                                                     channelName(channel, 3u), what, range->lowest, range->highest)
                                       : fmt::format("No {} sample of the {} means something; choose another "
                                                     "reference",
                                                     channelName(channel, 3u), what));
                }
                if (measured.failed())
                {
                    return measured;
                }
            }
            return values;
        },
        copy->working);
}

/// What a source's metadata becomes when an engine operation changed its samples: the same, with a lineage entry.
[[nodiscard]] an::Result<an::ImageMetadata> withLineage(const an::ImageMetadata& metadata, std::string operation,
                                                        std::string detail)
{
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(std::move(operation), std::move(detail));
    if (!entry.hasValue())
    {
        return entry.status();
    }
    return an::applyMetadataTransition(metadata, an::MetadataTransition{.record = *entry});
}

/// The bottom and the top of a representation's range: a fraction of zero and of one.
template <typename SampleT> [[nodiscard]] constexpr SampleT bottomOf() noexcept
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return SampleT{0};
    }
    else
    {
        return std::numeric_limits<SampleT>::lowest();
    }
}

template <typename SampleT> [[nodiscard]] constexpr SampleT topOf() noexcept
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return SampleT{1};
    }
    else
    {
        return (std::numeric_limits<SampleT>::max)();
    }
}

/// Whether a pixel lies in a region, or anywhere for none.
[[nodiscard]] bool inRegion(const std::optional<an::StatisticsRegion>& region, const std::uint32_t x,
                            const std::uint32_t y) noexcept
{
    return !region.has_value() || (x >= region->originX && y >= region->originY &&
                                   x - region->originX < region->width && y - region->originY < region->height);
}

/// The scaling match of a source to a reference and the source's working copy, which the apply writes.
struct LinearMatchWork
{
    an::FrameNormalization normalization;
    an::consumer::AnyImage working;
};

/// A checked normalization frame over a working copy, under a validity.
[[nodiscard]] an::Result<an::NormalizationFrameView> frameOver(const an::consumer::AnyImage& working,
                                                               const an::KernelValidityView& validity)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::NormalizationFrameView> {
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            const auto view = typed.view();
            if (!described.has_value() || !view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
            }
            return an::NormalizationFrameView::create(view->data(), view->layout().totalBytes(), view->layout(),
                                                      *described, validity);
        },
        working);
}

[[nodiscard]] an::Result<LinearMatchWork> linearMatchOf(const an::ui::ExecutionScope& scope,
                                                        const an::ui::ProcessSource& source,
                                                        const an::ui::ProcessSource& reference,
                                                        const FractionRange& range)
{
    const std::optional<an::ImageDescriptor> target = source.descriptor();
    const std::optional<an::ImageDescriptor> matched = reference.descriptor();
    if (!target.has_value() || !matched.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image or its reference holds nothing");
    }
    const std::uint32_t channels = target->extent().channelCount();
    if (matched->extent().channelCount() != channels)
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "Reference and target images must have the same number of channels");
    }
    if (channels > an::kMaxNormalizationChannels)
    {
        return refusal(an::ErrorCode::Unsupported,
                       fmt::format("a linear match reads at most {} channels", an::kMaxNormalizationChannels));
    }
    // The two images' samples are paired when they are one size, only the reference held to the limits, and each is
    // measured on its own, both held to the limits, when they are not; a zero means nothing in either.
    const bool paired = target->extent().width() == matched->extent().width() &&
                        target->extent().height() == matched->extent().height();
    an::Result<KeptCopy> targetCopy =
        keptCopyOf(scope, source, paired ? std::nullopt : std::optional<FractionRange>(range), true);
    if (!targetCopy.hasValue())
    {
        return targetCopy.status();
    }
    an::Result<KeptCopy> referenceCopy = keptCopyOf(scope, reference, range, true);
    if (!referenceCopy.hasValue())
    {
        return referenceCopy.status();
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> targetKept = std::as_const(targetCopy->kept).view();
    const std::optional<an::ValidityMaskView<const std::uint8_t>> referenceKept =
        std::as_const(referenceCopy->kept).view();
    if (!targetKept.has_value() || !referenceKept.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the kept samples' mask holds nothing");
    }
    const an::Result<an::NormalizationFrameView> targetFrame =
        frameOver(targetCopy->working, an::KernelValidityView{.descriptor = explicitMaskRule(), .mask = &*targetKept});
    if (!targetFrame.hasValue())
    {
        return targetFrame.status();
    }
    const an::Result<an::NormalizationFrameView> referenceFrame = frameOver(
        referenceCopy->working, an::KernelValidityView{.descriptor = explicitMaskRule(), .mask = &*referenceKept});
    if (!referenceFrame.hasValue())
    {
        return referenceFrame.status();
    }

    an::NormalizationReference measuredReference;
    an::NormalizationFrameStatistics measuredTarget;
    an::Status measured = an::Status::success();
    if (paired)
    {
        const an::StatisticsRegion whole{
            .originX = 0u, .originY = 0u, .width = target->extent().width(), .height = target->extent().height()};
        const an::Result<an::OverlapMeasurement> overlap =
            an::measureOverlapStatistics(scope.execution(), 0u, *referenceFrame, whole, 1u, *targetFrame, whole,
                                         an::ScaleEstimator::MedianAbsoluteDeviation);
        if (overlap.hasValue())
        {
            measuredReference.estimator = overlap->estimator;
            measuredReference.channelCount = overlap->channelCount;
            measuredReference.memberCount = 1u;
            measuredReference.channels = overlap->a;
            measuredTarget.estimator = overlap->estimator;
            measuredTarget.channelCount = overlap->channelCount;
            measuredTarget.channels = overlap->b;
        }
        measured = overlap.hasValue() ? an::Status::success() : overlap.status();
    }
    else
    {
        const an::Result<an::NormalizationFrameStatistics> referenceStatistics = an::measureGlobalNormalization(
            scope.execution(), *referenceFrame, an::ScaleEstimator::MedianAbsoluteDeviation);
        const an::Result<an::NormalizationFrameStatistics> targetStatistics = an::measureGlobalNormalization(
            scope.execution(), *targetFrame, an::ScaleEstimator::MedianAbsoluteDeviation);
        if (!referenceStatistics.hasValue())
        {
            measured = referenceStatistics.status();
        }
        else if (!targetStatistics.hasValue())
        {
            measured = targetStatistics.status();
        }
        else
        {
            const an::Result<an::NormalizationReference> combined = an::combineNormalizationReferences(
                std::span<const an::NormalizationFrameStatistics>{&*referenceStatistics, 1u});
            if (combined.hasValue())
            {
                measuredReference = *combined;
                measuredTarget = *targetStatistics;
            }
            measured = combined.hasValue() ? an::Status::success() : combined.status();
        }
    }
    if (measured.code() == an::ErrorCode::EmptySelection)
    {
        // The engine answers that a channel kept nothing, not which; the message names the measurement instead.
        return refusal(an::ErrorCode::EmptySelection,
                       paired ? fmt::format("A channel of the image and its reference shares no position whose "
                                            "reference sample lies between the limits {} and {} and where neither "
                                            "sample is zero; widen the limits or choose another reference",
                                            range.lowest, range.highest)
                              : fmt::format("A channel of the image or of its reference holds no sample that lies "
                                            "between the limits {} and {} and is not zero; widen the limits or choose "
                                            "another reference",
                                            range.lowest, range.highest));
    }
    if (measured.failed())
    {
        return measured;
    }
    an::Result<an::FrameNormalization> normalization =
        an::estimateGlobalNormalization(measuredReference, measuredTarget, an::NormalizationMethod::AdditiveScaling);
    if (!normalization.hasValue())
    {
        return normalization.status();
    }
    return LinearMatchWork{.normalization = *normalization, .working = std::move(targetCopy->working)};
}

/// Blends a run's result toward its source by a mask, in place: the framework's blend.
[[nodiscard]] an::Status blendByMask(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                                     an::ui::ProcessDestination& destination, const an::consumer::AnyImage& mask,
                                     const bool inverted)
{
    return an::ui::blendProcessResult(
        scope.engine(), scope.execution(), source, destination,
        an::ui::ProcessBlendRequest{.modulation = 1.0, .mask = &mask, .invertMask = inverted});
}

/// Weighs a run's result in its destination by a view's mask, read through the lease the run took before it was
/// admitted; nothing for none.
[[nodiscard]] an::Status weighDestinationByViewMask(const an::ui::ExecutionScope& scope,
                                                    const an::ui::ProcessSource& source,
                                                    an::ui::ProcessDestination& destination,
                                                    const ViewMaskSnapshot& mask)
{
    if (mask.asset == nullptr)
    {
        return an::Status::success();
    }
    an::Result<an::consumer::AssetReadLease> lease =
        an::ui::readBeside(scope, *mask.asset, scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return blendByMask(scope, source, destination, lease->image(), mask.inverted);
}

} // namespace

// ---- Snapshots.

ViewMaskSnapshot viewMaskOf(const ImageViewControl& view)
{
    ViewMaskSnapshot mask;
    if (view.isMasked() && view.isMaskEnabled())
    {
        if (const ImageViewControl* maskView = view.getCurrentMask(); maskView != nullptr && maskView->hasImage())
        {
            mask.asset = maskView->document().asset();
            mask.inverted = view.isMaskInverted();
        }
    }
    return mask;
}

bool showsRgbImage(const ImageViewControl* view)
{
    if (view == nullptr)
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> described = view->imageDescriptor();
    return described.has_value() && isCalibrationRgb(*described);
}

std::uint32_t channelCountOf(const ImageViewControl* view)
{
    if (view == nullptr)
    {
        return 0u;
    }
    const std::optional<an::ImageDescriptor> described = view->imageDescriptor();
    return described.has_value() ? described->extent().channelCount() : 0u;
}

std::optional<an::StatisticsRegion> regionOf(const QRect& rectangle, const an::ImageDescriptor& described)
{
    if (rectangle.x() == 0 && rectangle.y() == 0 &&
        static_cast<std::uint32_t>(rectangle.width()) == described.extent().width() &&
        static_cast<std::uint32_t>(rectangle.height()) == described.extent().height())
    {
        return std::nullopt;
    }
    return an::StatisticsRegion{.originX = static_cast<std::uint32_t>(rectangle.x()),
                                .originY = static_cast<std::uint32_t>(rectangle.y()),
                                .width = static_cast<std::uint32_t>(rectangle.width()),
                                .height = static_cast<std::uint32_t>(rectangle.height())};
}

bool rectangleInside(const QRect& rectangle, const an::ImageDescriptor& described)
{
    return rectangle.width() > 0 && rectangle.height() > 0 && rectangle.x() >= 0 && rectangle.y() >= 0 &&
           static_cast<std::uint64_t>(rectangle.x()) + static_cast<std::uint64_t>(rectangle.width()) <=
               described.extent().width() &&
           static_cast<std::uint64_t>(rectangle.y()) + static_cast<std::uint64_t>(rectangle.height()) <=
               described.extent().height();
}

// ---- Reading a reference.

ReferenceRead::ReferenceRead(const an::ui::ProcessSource& target) noexcept : m_target(target) {}

ReferenceRead::ReferenceRead(an::consumer::AssetReadLease&& lease) noexcept : m_lease(std::move(lease)) {}

an::Result<ReferenceRead> ReferenceRead::read(const an::ui::ExecutionScope& scope, const ToolReference& reference,
                                              const an::ui::ProcessSource& target)
{
    if (reference.asset == nullptr)
    {
        return ReferenceRead(target);
    }
    an::Result<an::consumer::AssetReadLease> lease =
        an::ui::readBeside(scope, *reference.asset, scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return ReferenceRead(std::move(*lease));
}

an::ui::ProcessSource ReferenceRead::source() const noexcept
{
    return m_lease.has_value() ? an::ui::ProcessSource::of(*m_lease) : *m_target;
}

// ---- Measurements.

an::Result<an::ColorChannelValues> measureChannelMedians(const an::ui::ExecutionScope& scope,
                                                         const an::ui::ProcessSource& reference,
                                                         const std::optional<an::StatisticsRegion>& region,
                                                         const std::optional<FractionRange>& range,
                                                         const std::string_view what)
{
    return measureChannels(scope, reference, region, range, what, Statistic::Median);
}

an::Result<an::ColorChannelValues> measureChannelMeans(const an::ui::ExecutionScope& scope,
                                                       const an::ui::ProcessSource& reference,
                                                       const std::optional<an::StatisticsRegion>& region,
                                                       const std::optional<FractionRange>& range,
                                                       const std::string_view what)
{
    return measureChannels(scope, reference, region, range, what, Statistic::Mean);
}

an::Result<an::consumer::ImageAsset> selectedSamplesMask(const an::ui::ExecutionScope& scope,
                                                         const an::ui::ProcessSource& reference,
                                                         const std::optional<an::StatisticsRegion>& region,
                                                         const FractionRange& range)
{
    const std::optional<an::ImageDescriptor> described = reference.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the reference holds nothing");
    }
    an::Result<KeptCopy> copy = keptCopyOf(scope, reference, range, false);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> kept = std::as_const(copy->kept).view();
    if (!kept.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the kept samples' mask holds nothing");
    }
    an::Result<an::ImageDescriptor> maskDescribed =
        process_utils::derivedImageDescriptor(*described, 1u, described->sampleType(), scope.engine().limits());
    if (!maskDescribed.hasValue())
    {
        return maskDescribed.status();
    }
    an::Result<an::ImageMetadata> metadata =
        process_utils::derivedImageMetadata(reference.metadata(), *maskDescribed, std::string());
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    an::Result<an::consumer::AnyImage> mask = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = SampleOf<decltype(typed)>;
            an::Result<an::Image<SampleT>> image =
                an::Image<SampleT>::create(scope.engine(), *maskDescribed, an::StorageArrangement::Planar,
                                           an::StorageInitialization::Uninitialized, std::move(*metadata));
            if (!image.hasValue())
            {
                return image.status();
            }
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the mask holds nothing");
                }
                const an::ImageView<SampleT> written = access->view();
                const std::uint32_t channels = kept->planeCount();
                for (std::uint32_t y = 0u; y < written.height(); ++y)
                {
                    SampleT* sample = written.sampleAt(0u, 0u, y);
                    for (std::uint32_t x = 0u; x < written.width(); ++x, ++sample)
                    {
                        bool selected = inRegion(region, x, y);
                        for (std::uint32_t channel = 0u; channel < channels && selected; ++channel)
                        {
                            selected = kept->test(channel, x, y).value_or(false);
                        }
                        *sample = selected ? topOf<SampleT>() : bottomOf<SampleT>();
                    }
                }
            }
            return an::consumer::AnyImage(std::move(*image));
        },
        reference.image());
    if (!mask.hasValue())
    {
        return mask.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(*mask));
}

an::Result<NeutralizationMeasurement> measureNeutralization(const an::ui::ExecutionScope& scope,
                                                            const an::ui::ProcessSource& reference,
                                                            const std::optional<an::StatisticsRegion>& region,
                                                            const FractionRange& range)
{
    an::Result<an::ColorChannelValues> levels =
        measureChannelMedians(scope, reference, region, range, "background reference");
    if (!levels.hasValue())
    {
        return levels.status();
    }
    NeutralizationMeasurement measurement;
    measurement.levels = *levels;
    measurement.pedestal = an::meanBackgroundPedestal(*levels);
    measurement.offsets = an::colorCalibrationOffsets(
        {1.0, 1.0, 1.0}, *levels, an::ColorCalibrationBackgroundMode::SharedPedestal, measurement.pedestal);
    return measurement;
}

an::Result<ColorCalibrationMeasurement> measureColorCalibration(const an::ui::ExecutionScope& scope,
                                                                const an::ui::ProcessSource* white,
                                                                const an::ui::ProcessSource& background,
                                                                const ColorCalibrationRequest& request)
{
    ColorCalibrationMeasurement measurement;
    // The background level is the median of every sample of the background reference's rectangle; the limits decide
    // only the samples its mask selects.
    an::Result<an::ColorChannelValues> levels =
        measureChannelMedians(scope, background, request.backgroundRegion, std::nullopt, "background reference");
    if (!levels.hasValue())
    {
        return levels.status();
    }
    measurement.background = *levels;
    if (request.manualWhite.has_value())
    {
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const double gain = (*request.manualWhite)[channel];
            if (!std::isfinite(gain) || gain <= 0.0)
            {
                return refusal(an::ErrorCode::InvalidArgument,
                               fmt::format("The manual {} white value must be above zero", channelName(channel, 3u)));
            }
        }
        measurement.white = *request.manualWhite;
        measurement.gains = *request.manualWhite;
    }
    else
    {
        if (white == nullptr)
        {
            return refusal(an::ErrorCode::InvalidArgument, "A valid white reference image is required.");
        }
        an::Result<an::ColorChannelValues> whiteLevels =
            measureChannelMeans(scope, *white, request.whiteRegion, request.whiteRange, "white reference");
        if (!whiteLevels.hasValue())
        {
            return whiteLevels.status();
        }
        measurement.white = *whiteLevels;
        // The gain rule: each channel's white above its background, normalized to the strongest channel - the later of
        // two equal ones, which gains one either way.
        an::ColorChannelValues above{};
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            above[channel] = std::fabs(measurement.white[channel] - measurement.background[channel]);
        }
        const std::uint32_t strongest =
            above[0] > above[1] ? (above[0] > above[2] ? 0u : 2u) : (above[1] > above[2] ? 1u : 2u);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            if (!(above[channel] > 0.0) || !std::isfinite(above[channel]))
            {
                return refusal(an::ErrorCode::InvalidData,
                               fmt::format("The {} white level of the white reference sits on its background level, "
                                           "so no gain balances it; choose another reference or other limits",
                                           channelName(channel, 3u)));
            }
            measurement.gains[channel] = channel == strongest ? 1.0 : above[strongest] / above[channel];
        }
    }
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        measurement.offsets[channel] = measurement.background[channel] * (1.0 - measurement.gains[channel]);
    }
    return measurement;
}

// ---- The applies.

an::Result<an::consumer::ImageAsset> applyChannelGains(const an::ui::ExecutionScope& scope,
                                                       const an::ui::ProcessSource& source,
                                                       const ChannelGainsApply& apply, const ViewMaskSnapshot& mask)
{
    an::Result<an::consumer::AnyImage> working =
        makeCalibrationWorkingImage(scope.engine(), scope.execution(), source.image());
    if (!working.hasValue())
    {
        return working.status();
    }
    an::Result<std::unique_ptr<WorkingValidity>> validity = WorkingValidity::of(scope.engine(), source);
    if (!validity.hasValue())
    {
        return validity.status();
    }
    const an::Status applied = std::visit(
        [&](auto& typed) -> an::Status {
            using WorkT = SampleOf<decltype(typed)>;
            if constexpr (!std::is_floating_point_v<WorkT>)
            {
                return refusal(an::ErrorCode::Internal, "a working image holds floating samples");
            }
            else
            {
                const std::optional<an::ImageDescriptor> described = typed.descriptor();
                std::optional<an::ImageWriteAccess<WorkT>> access = typed.write();
                if (!described.has_value() || !access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the working image holds nothing");
                }
                const an::ImageLayout layout = access->view().layout();
                void* samples = access->view().data();
                const an::Result<an::StarFrameView> frame =
                    an::StarFrameView::create(samples, layout.totalBytes(), layout, *described, (*validity)->view());
                if (!frame.hasValue())
                {
                    return frame.status();
                }
                const an::Result<an::ColorCalibrationApplyResult> calibrated = an::applyColorCalibration(
                    scope.execution(), *frame, samples, layout.totalBytes(),
                    an::ColorCalibrationApplyRequest{.gains = apply.gains,
                                                     .offsets = apply.offsets,
                                                     .clipping = an::ColorCalibrationClipping::ClampToPixelRange});
                return calibrated.hasValue() ? an::Status::success() : calibrated.status();
            }
        },
        *working);
    if (applied.failed())
    {
        return applied;
    }
    an::Result<an::ImageMetadata> metadata = withLineage(
        source.metadata(), "an::applyColorCalibration",
        fmt::format("{}: gains {:.6f}, {:.6f}, {:.6f}; offsets {:.6f}, {:.6f}, {:.6f}", apply.lineage, apply.gains[0],
                    apply.gains[1], apply.gains[2], apply.offsets[0], apply.offsets[1], apply.offsets[2]));
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    an::Result<an::consumer::ImageAsset> result =
        calibratedAsset(scope.session(), scope.engine(), scope.execution(), source, *working, std::move(*metadata));
    if (!result.hasValue())
    {
        return result.status();
    }
    return weighByViewMask(scope, source, std::move(*result), mask);
}

const std::vector<QString>& scnrProtectionLabels()
{
    static const std::vector<QString> labels{QStringLiteral("Average Neutral"), QStringLiteral("Maximum Neutral"),
                                             QStringLiteral("Maximum Mask"), QStringLiteral("Additive Mask")};
    return labels;
}

std::optional<an::ChromaticNoiseMethod> scnrMethodFromLabel(const QString& label)
{
    static constexpr std::array<an::ChromaticNoiseMethod, 4u> kMethods{
        an::ChromaticNoiseMethod::AverageNeutral, an::ChromaticNoiseMethod::MaximumNeutral,
        an::ChromaticNoiseMethod::MaximumMask, an::ChromaticNoiseMethod::AdditiveMask};
    const std::vector<QString>& labels = scnrProtectionLabels();
    for (std::size_t index = 0u; index < labels.size(); ++index)
    {
        if (labels[index] == label)
        {
            return kMethods[index];
        }
    }
    return std::nullopt;
}

an::Result<an::consumer::ImageAsset> suppressGreen(const an::ui::ExecutionScope& scope,
                                                   const an::ui::ProcessSource& source,
                                                   const GreenSuppression& suppression, const ViewMaskSnapshot& mask)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (!isCalibrationRgb(*described))
    {
        return refusal(an::ErrorCode::Unsupported, "SCNR needs an RGB image");
    }
    const an::Result<an::ChromaticNoisePlan> plan = an::planChromaticNoiseReduction(an::ChromaticNoiseRequest{
        .method = suppression.method,
        .amount = suppression.amount,
        .whiteLevel = 1.0,
        .preserveLightness = suppression.preserveLightness,
        .rgbEncoding = an::ui::processChannelScopeRequest(an::ChannelScope::Whole, *described).sourceEncoding});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::consumer::AnyImage> input =
        makeStretchWorkingCopy(scope.engine(), scope.execution(), source.image());
    if (!input.hasValue())
    {
        return input.status();
    }
    // The output starts as the input, so a pixel the validity rejects is left as it was.
    an::Result<an::consumer::AnyImage> output =
        makeStretchWorkingCopy(scope.engine(), scope.execution(), source.image());
    if (!output.hasValue())
    {
        return output.status();
    }
    an::Result<std::unique_ptr<WorkingValidity>> validity = WorkingValidity::of(scope.engine(), source);
    if (!validity.hasValue())
    {
        return validity.status();
    }
    const an::SpatialSelection selection{.validity = (*validity)->view().descriptor, .mask = (*validity)->view().mask};
    const an::Status reduced = std::visit(
        [&](const auto& from) -> an::Status {
            using WorkT = SampleOf<decltype(from)>;
            auto* to = std::get_if<an::Image<WorkT>>(&*output);
            const auto read = from.view();
            if (to == nullptr || !read.has_value())
            {
                return refusal(an::ErrorCode::Internal, "the working copies differ");
            }
            std::optional<an::ImageWriteAccess<WorkT>> access = to->write();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
            }
            const an::Result<an::NoiseReductionResult> ran =
                an::reduceImageChromaticNoise(scope.execution(), an::kSampleTypeOf<WorkT>, read->data(), read->layout(),
                                              access->view().data(), access->view().layout(), *plan, selection);
            return ran.hasValue() ? an::Status::success() : ran.status();
        },
        *input);
    if (reduced.failed())
    {
        return reduced;
    }
    an::Result<an::consumer::ImageAsset> result =
        calibratedAsset(scope.session(), scope.engine(), scope.execution(), source, *output, source.metadata());
    if (!result.hasValue())
    {
        return result.status();
    }
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(scope.session(), scope.engine(), scope.execution(), source);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    if (an::Status replaced = destination->replace(std::move(*result)); replaced.failed())
    {
        return replaced;
    }
    // The result is recorded nonlinear, under the process's name, on the processing-state cards.
    if (an::Status recorded = process_utils::recordNonlinearResult(
            *destination,
            process_utils::NonlinearResultRecord{
                .stateOperation = "scnr",
                .engineOperation = "an::reduceImageChromaticNoise",
                .detail = fmt::format("SCNR: {}, amount {:.6f}, lightness {}",
                                      an::chromaticNoiseMethodName(suppression.method), suppression.amount,
                                      suppression.preserveLightness ? "preserved" : "not preserved")});
        recorded.failed())
    {
        return recorded;
    }
    if (an::Status weighed = weighDestinationByViewMask(scope, source, *destination, mask); weighed.failed())
    {
        return weighed;
    }
    return destination->release();
}

an::Result<an::FrameNormalization> measureLinearMatch(const an::ui::ExecutionScope& scope,
                                                      const an::ui::ProcessSource& source,
                                                      const an::ui::ProcessSource& reference,
                                                      const FractionRange& range)
{
    an::Result<LinearMatchWork> work = linearMatchOf(scope, source, reference, range);
    if (!work.hasValue())
    {
        return work.status();
    }
    return work->normalization;
}

an::Result<an::consumer::ImageAsset> matchLinearly(const an::ui::ExecutionScope& scope,
                                                   const an::ui::ProcessSource& source,
                                                   const an::ui::ProcessSource& reference, const FractionRange& range)
{
    an::Result<LinearMatchWork> work = linearMatchOf(scope, source, reference, range);
    if (!work.hasValue())
    {
        return work.status();
    }
    an::Result<std::unique_ptr<WorkingValidity>> validity = WorkingValidity::of(scope.engine(), source);
    if (!validity.hasValue())
    {
        return validity.status();
    }
    // Every sample the image's own validity admits is written, whatever the limits measured: they chose the samples
    // the match was measured over, not the ones it changes.
    const an::Status applied = std::visit(
        [&](auto& typed) -> an::Status {
            using WorkT = SampleOf<decltype(typed)>;
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            std::optional<an::ImageWriteAccess<WorkT>> access = typed.write();
            if (!described.has_value() || !access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
            }
            const an::ImageLayout layout = access->view().layout();
            void* samples = access->view().data();
            const an::Result<an::NormalizationFrameView> frame = an::NormalizationFrameView::create(
                samples, layout.totalBytes(), layout, *described, (*validity)->view());
            if (!frame.hasValue())
            {
                return frame.status();
            }
            const an::Result<an::NormalizationApplyResult> ran = an::applyGlobalNormalization(
                scope.execution(), work->normalization, *frame, samples, layout.totalBytes(), layout);
            return ran.hasValue() ? an::Status::success() : ran.status();
        },
        work->working);
    if (applied.failed())
    {
        return applied;
    }
    std::string detail = "linear match:";
    for (std::uint32_t channel = 0u; channel < work->normalization.channelCount; ++channel)
    {
        detail +=
            fmt::format(" {} scale {:.6f} offset {:.6f};", channelName(channel, work->normalization.channelCount),
                        work->normalization.channels[channel].scale, work->normalization.channels[channel].offset);
    }
    an::Result<an::ImageMetadata> metadata =
        withLineage(source.metadata(), "an::applyGlobalNormalization", std::move(detail));
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    return calibratedAsset(scope.session(), scope.engine(), scope.execution(), source, work->working,
                           std::move(*metadata));
}

an::Result<an::consumer::ImageAsset> weighByViewMask(const an::ui::ExecutionScope& scope,
                                                     const an::ui::ProcessSource& source,
                                                     an::consumer::ImageAsset&& result, const ViewMaskSnapshot& mask)
{
    if (mask.asset == nullptr)
    {
        return std::move(result);
    }
    an::Result<an::consumer::AssetReadLease> lease =
        an::ui::readBeside(scope, *mask.asset, scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(scope.session(), scope.engine(), scope.execution(), source);
    if (!destination.hasValue())
    {
        return destination.status();
    }
    if (an::Status replaced = destination->replace(std::move(result)); replaced.failed())
    {
        return replaced;
    }
    if (an::Status blended = blendByMask(scope, source, *destination, lease->image(), mask.inverted); blended.failed())
    {
        return blended;
    }
    return destination->release();
}

} // namespace an::ui::processes
