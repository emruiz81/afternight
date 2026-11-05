// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a registration computes on the engine: the frame read and widened, the stars detected and summarized, the
/// chromatic aberration corrected channel by channel, the frames matched by their stars - to the reference, or through
/// a frame registered near them - or by phase correlation, the catalog's figures fitted through the star profilers'
/// enrichment, and each frame warped onto its canvas with its plate distortion undone.

#include "stdafx.hpp"
#include "processes/core/RegistrationRun.h"

#include "controls/ImageViewFiles.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/StarProfilerSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Registration/PhaseCorrelation.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stars/PsfFit.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::ui::processes
{

namespace
{

using ImageIntegration::PersistedChoice;

/// The most stars a channel's detection keeps for the chromatic aberration's match.
constexpr std::uint32_t kChromaticAberrationMaximumStars = 4096u;

/// The fewest stars either side of a channel's match needs.
constexpr std::size_t kChromaticAberrationMinimumPoints = 3u;

/// A match of fewer candidate pairs than this is held to a share of inliers among them.
constexpr std::uint32_t kSmallStarMatchCandidates = 10u;

/// A field of this many stars or more, on either side of a match, exempts a small match from that share.
constexpr std::uint32_t kCrowdedStarFieldStars = 1000u;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// What a star match asks of the engine: the model, at least the pairs asked for - never fewer than five, never more
/// than a frame can hold - and the smallest share of inliers among the candidate pairs the engine admits, which no
/// match of five pairs or more falls under, so that the engine's answer is held to `checkRegistrationStarMatch` alone.
/// The engine still refuses a match whose robust fit, or whose final least-squares model, keeps fewer than the pairs,
/// and a set whose inliers are degenerate for the model.
[[nodiscard]] an::StarAlignmentRequest starMatchRequest(const an::StarTransformModel model,
                                                        const std::uint32_t minimumPairs) noexcept
{
    return an::StarAlignmentRequest{
        .model = model,
        .minimumMatches = (std::min)((std::max)(minimumPairs, kReliableMinimumStarPairs), an::kMaxStarsPerFrame),
        .minimumInlierFraction = std::numeric_limits<double>::min()};
}

/// Whether a registration's stars are fitted by the Moffat profile, the one that reads the smallest exponent.
[[nodiscard]] bool fitsMoffatProfiles(const RegistrationSettings& settings) noexcept
{
    return settings.profileType.compare(QStringLiteral("Moffat"), Qt::CaseInsensitive) == 0;
}

/// The search radius the detector runs with: the settings', held to the radii the detector takes.
[[nodiscard]] std::uint32_t detectorRadiusOf(const RegistrationSettings& settings) noexcept
{
    return static_cast<std::uint32_t>(
        std::clamp(settings.radius, 1, ImageIntegration::RegistrationSettingsUtils::kLargestStarSearchRadius));
}

/// The roundness threshold the detector runs with: the settings', or the smallest it takes for one that is not
/// positive, and at most one.
[[nodiscard]] double detectorRoundnessOf(const RegistrationSettings& settings) noexcept
{
    if (!(settings.roundnessThreshold > 0.0))
    {
        return ImageIntegration::RegistrationSettingsUtils::kSmallestStarRoundness;
    }
    return (std::min)(settings.roundnessThreshold, 1.0);
}

/// The smallest Moffat exponent the detector runs with: the settings', or the smallest it takes for one that is not
/// positive, and at most its ceiling. The detector checks it under every profile.
[[nodiscard]] double detectorBetaOf(const RegistrationSettings& settings) noexcept
{
    if (!(settings.minimumBeta > 0.0))
    {
        return ImageIntegration::RegistrationSettingsUtils::kSmallestMoffatBeta;
    }
    return (std::min)(settings.minimumBeta, an::kMoffatBetaCeiling);
}

/// Whether a refused fit is a stop or a refused allocation, which no other model answers.
[[nodiscard]] bool stopsTheFit(const an::Status& status) noexcept
{
    switch (status.code())
    {
    case an::ErrorCode::Cancelled:
    case an::ErrorCode::DeadlineExceeded:
    case an::ErrorCode::MemoryBudgetExceeded:
    case an::ErrorCode::AllocationFailed:
        return true;
    default:
        return false;
    }
}

/// What a frame says when it is registered by the linear transform instead of the polynomial order its settings state,
/// worded to follow its name as `RegisteredFrame::registrationNote` is.
///
/// @param pairs the star pairs the frame was registered by.
/// @param order the order the settings state.
/// @param anchor the frame those pairs were matched to, when it is not the reference.
/// @param refusal why the order's fit refused the pairs; null when they are fewer than the order's terms.
[[nodiscard]] QString linearInsteadOfOrderNote(const std::size_t pairs, const std::uint32_t order,
                                               const QString& anchor, const char* refusal)
{
    const QString matched =
        anchor.isEmpty()
            ? QStringLiteral("matched %1 star pairs").arg(pairs)
            : QStringLiteral("matched %1 star pairs through %2").arg(pairs).arg(QFileInfo(anchor).fileName());
    if (refusal == nullptr)
    {
        return QStringLiteral("%1, fewer than the %2 a polynomial of order %3 needs, and is registered with the linear "
                              "transform")
            .arg(matched, QString::number(an::registrationPolynomialTermCount(order)), QString::number(order));
    }
    return QStringLiteral("%1, but a polynomial of order %2 could not be fitted to them (%3), and is registered with "
                          "the linear transform")
        .arg(matched, QString::number(order), QString::fromUtf8(refusal));
}

/// A Float32 copy of an image with every sample a fraction of full scale, in the stored description otherwise.
[[nodiscard]] an::Result<an::Image<float>> fractionsOf(const an::consumer::OperationScope& scope,
                                                       const an::consumer::AnyImage& image)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::Image<float>> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const std::optional<an::ImageDescriptor> stored = typed.descriptor();
            const auto view = typed.view();
            if (!stored.has_value() || !view.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
            }
            an::ImageDescriptorRequest request = stored->toRequest();
            request.sampleType = an::SampleType::Float32;
            request.numericDomain = an::NumericDomain::NormalizedLinear;
            const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
            if (!described.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments, "the frame has no Float32 description");
            }
            an::Result<an::Image<float>> widened =
                an::Image<float>::create(scope.engine(), *described, an::StorageArrangement::Planar,
                                         an::StorageInitialization::Uninitialized, typed.metadata());
            if (!widened.hasValue())
            {
                return widened.status();
            }
            {
                std::optional<an::ImageWriteAccess<float>> access = widened->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the widened frame holds nothing");
                }
                an::ConversionCounters counters;
                const an::Status converted =
                    an::convertImageSamples(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                            an::SampleType::Float32, access->view().data(), access->view().layout(),
                                            an::ConversionPolicy{}, counters);
                if (converted.failed())
                {
                    return converted;
                }
            }
            return std::move(*widened);
        },
        image);
}

/// Which samples of a frame the stars are measured and searched over when it holds one that is not finite: every finite
/// one. A calibration writes a sample that is not a number where its flat is zero, and a light it calibrated keeps it
/// in memory and in the file it is written to; the detector measures the sky without such a sample and searches the sky
/// in its place, where a frame read as all valid would have the detector refuse it.
constexpr an::ValidityDescriptor kFiniteSamples =
    *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});

/// The exponent bits of a single-precision sample, all set in every infinity and every not-a-number alone.
constexpr std::uint32_t kFloatExponentBits = 0x7f800000u;

/// Whether one channel of a Float32 frame holds a sample that is not finite: one pass over its rows, each row read
/// without a branch so that it vectorizes. A channel whose rows the view does not lend is answered as holding one, so
/// that it is read over its finite samples, which is right either way.
[[nodiscard]] bool holdsNonFiniteSample(const an::Image<float>& frame, const std::uint32_t channel)
{
    const auto view = frame.view();
    if (!view.has_value())
    {
        return false;
    }
    for (std::uint32_t y = 0u; y < view->height(); ++y)
    {
        const std::optional<std::span<const float>> row = view->row(channel, y);
        if (!row.has_value())
        {
            return true;
        }
        std::uint32_t nonFinite = 0u;
        for (const float sample : *row)
        {
            nonFinite |= static_cast<std::uint32_t>((std::bit_cast<std::uint32_t>(sample) & kFloatExponentBits) ==
                                                    kFloatExponentBits);
        }
        if (nonFinite != 0u)
        {
            return true;
        }
    }
    return false;
}

/// The star frame view of a Float32 image measured in one channel, its validity that channel's, borrowing the layout
/// the caller keeps.
[[nodiscard]] an::Result<an::StarFrameView> starFrameOf(const an::Image<float>& frame, const an::ImageLayout& layout,
                                                        const std::uint32_t channel)
{
    const auto view = frame.view();
    const std::optional<an::ImageDescriptor> descriptor = frame.descriptor();
    if (!view.has_value() || !descriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    return an::StarFrameView::create(view->data(), layout.totalBytes(), layout, *descriptor,
                                     registrationChannelValidity(frame, channel));
}

/// The stars of one channel, with the channel's statistics measured by the detection or handed to it.
[[nodiscard]] an::Result<an::StarDetectionResult> detectChannel(const an::ExecutionContext& execution,
                                                                const an::Image<float>& frame,
                                                                const an::StarDetectionRequest& request,
                                                                const std::uint32_t channel,
                                                                const an::StarChannelStatistics* measured = nullptr)
{
    const auto view = frame.view();
    const std::optional<an::ImageDescriptor> descriptor = frame.descriptor();
    if (!view.has_value() || !descriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    // The frame view borrows its layout, so the layout it is given outlives the detection.
    const an::ImageLayout layout = view->layout();
    const an::Result<an::StarFrameView> starFrame = starFrameOf(frame, layout, channel);
    if (!starFrame.hasValue())
    {
        return starFrame.status();
    }
    const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(request, *descriptor, layout, channel);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::StarDetectionResult result;
    const an::Status found = measured != nullptr ? an::detectStars(execution, *starFrame, *plan, *measured, result)
                                                 : an::detectStars(execution, *starFrame, *plan, result);
    if (found.failed())
    {
        return found;
    }
    return result;
}

/// The channel a frame's stars are searched in: the green of a colour frame, the only one of a grey frame.
[[nodiscard]] std::uint32_t searchChannelOf(const an::Image<float>& frame)
{
    const std::optional<an::ImageDescriptor> descriptor = frame.descriptor();
    return descriptor.has_value() && descriptor->extent().channelCount() >= 3u ? 1u : 0u;
}

/// The linear model a transform text names.
[[nodiscard]] std::optional<std::pair<an::StarTransformModel, an::RegistrationTransformKind>>
linearModelOf(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("similarity"))
    {
        return std::pair{an::StarTransformModel::Similarity, an::RegistrationTransformKind::Similarity};
    }
    if (normalized == QStringLiteral("affine"))
    {
        return std::pair{an::StarTransformModel::Affine, an::RegistrationTransformKind::Affine};
    }
    if (normalized == QStringLiteral("homography") || normalized == QStringLiteral("projective"))
    {
        return std::pair{an::StarTransformModel::Projective, an::RegistrationTransformKind::Projective};
    }
    return std::nullopt;
}

/// The warp of an image onto a canvas, into a new image and the mask of what the image reaches.
struct Warped
{
    an::Image<float> image;
    an::ValidityMask mask;
};

/// The warp of an image onto a canvas through its transform, its plate distortion undone after the transform's inverse.
[[nodiscard]] an::Result<Warped> warpOnto(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                          const an::Image<float>& source, const an::RegistrationTransform& transform,
                                          const an::SipDistortion& distortion,
                                          const an::RegistrationWarpRequest& request)
{
    const std::optional<an::ImageDescriptor> descriptor = source.descriptor();
    const auto view = source.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    const an::Result<an::RegistrationWarpPlan> plan =
        an::planRegistrationWarp(*descriptor, transform, distortion, request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<float>> warped =
        an::Image<float>::create(engine, plan->destinationDescriptor(), an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, source.metadata());
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, plan->destinationDescriptor().extent());
    if (!warped.hasValue())
    {
        return warped.status();
    }
    if (!mask.hasValue())
    {
        return mask.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = warped->write();
        std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
        if (!access.has_value() || !maskView.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the warped frame holds nothing");
        }
        const an::Result<an::RegistrationWarpResult> ran = an::warpRegisteredImage(
            execution, an::SampleType::Float32, view->data(), view->layout(), access->view().data(),
            access->view().layout(), *plan, an::KernelValidityView{}, *maskView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change = an::operationTransition(
        an::kRegistrationWarpEffect,
        an::OperationOutcome{.operation = "an::warpRegisteredImage", .detail = "onto the registration's reference"});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = warped->transition(plan->destinationDescriptor(), *change); stated.failed())
    {
        return stated;
    }
    return Warped{.image = std::move(*warped), .mask = std::move(*mask)};
}

/// The stars of a detection outside the stated fraction of the half-diagonal from the centre, where the lateral
/// chromatic aberration shows.
[[nodiscard]] std::vector<an::StarAlignmentPoint> edgeStars(const std::vector<an::DetectedStar>& stars,
                                                            const std::uint32_t width, const std::uint32_t height,
                                                            const double edgeFraction)
{
    const double cx = static_cast<double>(width) * 0.5;
    const double cy = static_cast<double>(height) * 0.5;
    const double minimumRadius = std::clamp(edgeFraction, 0.0, 0.95) * std::hypot(cx, cy);
    std::vector<an::StarAlignmentPoint> points;
    for (const an::DetectedStar& star : stars)
    {
        if (std::hypot(star.positionX - cx, star.positionY - cy) >= minimumRadius)
        {
            points.push_back(an::StarAlignmentPoint{.x = star.positionX, .y = star.positionY});
        }
    }
    return points;
}

/// Whether a distortion corrects a frame's positions: only its forward polynomials do, so a model with inverse
/// polynomials alone corrects nothing a star is matched at, and the warp and the canvas undo nothing of it either.
[[nodiscard]] bool correctsPositions(const an::SipDistortion& distortion) noexcept
{
    return distortion.hasForwardModel();
}

/// The positions a frame's stars are matched at: where the explicit source distortion its record states corrects them
/// to, a star it cannot correct left out, and where they were detected under any other record.
[[nodiscard]] std::vector<an::StarAlignmentPoint> matchingPointsOf(const RegisteredFrame& frame)
{
    const an::SipDistortion* distortion =
        frame.distortion.policy == stage_utils::SourceDistortionPolicy::ExplicitModel &&
                frame.distortion.model.has_value() && correctsPositions(*frame.distortion.model)
            ? &*frame.distortion.model
            : nullptr;
    std::vector<an::StarAlignmentPoint> points;
    points.reserve(frame.stars.size());
    for (const RegisteredStar& star : frame.stars)
    {
        an::PixelCoordinate position{.x = star.x, .y = star.y};
        if (distortion != nullptr)
        {
            const an::Result<an::PixelCoordinate> corrected = an::applySipForward(*distortion, position);
            if (!corrected.hasValue())
            {
                continue;
            }
            position = *corrected;
        }
        points.push_back(an::StarAlignmentPoint{.x = position.x, .y = position.y});
    }
    return points;
}

/// One frame of a set being matched, in the set's order: its record and the positions its stars are matched at.
struct MatchingFrame
{
    const QString* path = nullptr;
    RegisteredFrame* frame = nullptr;
    std::vector<an::StarAlignmentPoint> points;
};

/// Registers the frames the reference did not match through the frames registered near them, as
/// `matchRegistrationStars` states.
///
/// @param execution the caller's admitted execution.
/// @param frames the set's frames in its order.
/// @param referenceIndex the reference's index among them.
/// @param model how the frames are matched.
/// @param fit the fit over the pairs a match makes, its normalization stated.
/// @return success; or a cancellation.
[[nodiscard]] an::Status registerThroughNeighbours(const an::ExecutionContext& execution,
                                                   std::vector<MatchingFrame>& frames, const std::size_t referenceIndex,
                                                   const RegistrationModel& model,
                                                   const an::RegistrationFitRequest& fit)
{
    const std::size_t count = frames.size();
    const auto holdsEnoughStars = [&frames, &model](const std::size_t index) {
        return frames[index].points.size() >= static_cast<std::size_t>(model.minimumMatches);
    };
    // A frame is tried through another this run registered from its stars; the reference matched it already.
    const auto registeredAnchor = [&](const std::size_t index) {
        const RegisteredFrame& frame = *frames[index].frame;
        return index != referenceIndex && !frame.failed && frame.transform.has_value() && holdsEnoughStars(index);
    };
    std::vector<std::size_t> unregistered;
    for (std::size_t index = 0u; index < count; ++index)
    {
        const RegisteredFrame& frame = *frames[index].frame;
        if (index != referenceIndex && (frame.failed || !frame.transform.has_value()))
        {
            unregistered.push_back(index);
        }
    }
    if (unregistered.empty())
    {
        return an::Status::success();
    }
    LOG_INFO("Image Registration: retrying {} unregistered frame(s) through the frames registered nearest them",
             unregistered.size());

    const auto nearestAnchorDistance = [&](const std::size_t index) {
        for (std::size_t distance = 1u; distance < count; ++distance)
        {
            if ((index >= distance && registeredAnchor(index - distance)) ||
                (index + distance < count && registeredAnchor(index + distance)))
            {
                return distance;
            }
        }
        return count;
    };
    const std::uint32_t requiredPairs =
        (std::max)(model.minimumMatches, an::minimumRegistrationControlPoints(model.fit.linearKind));
    // An anchor's stars are prepared when a frame is first tried through it, and the anchors used most recently are
    // kept prepared, no more than one frame's window of them: a preparation is the caller's storage, outside the engine
    // context's budget, and it is deterministic, so preparing an anchor again changes no answer.
    struct PreparedAnchor
    {
        std::size_t anchor = 0u;
        std::uint64_t lastUsed = 0u;
        an::PreparedStarAlignmentReference prepared;
    };
    std::vector<PreparedAnchor> preparedAnchors;
    preparedAnchors.reserve(kMaximumPreparedRegistrationAnchors);
    std::uint64_t anchorUses = 0u;
    std::set<std::size_t> unpreparedAnchors;
    const auto preparedAnchor =
        [&](const std::size_t anchorIndex) -> an::Result<const an::PreparedStarAlignmentReference*> {
        ++anchorUses;
        for (PreparedAnchor& entry : preparedAnchors)
        {
            if (entry.anchor == anchorIndex)
            {
                entry.lastUsed = anchorUses;
                return &entry.prepared;
            }
        }
        an::Result<an::PreparedStarAlignmentReference> made = an::prepareStarAlignmentReference(
            execution, frames[anchorIndex].points, starMatchRequest(model.starModel, model.minimumMatches));
        if (!made.hasValue())
        {
            return made.status();
        }
        if (preparedAnchors.size() < kMaximumPreparedRegistrationAnchors)
        {
            preparedAnchors.push_back(
                PreparedAnchor{.anchor = anchorIndex, .lastUsed = anchorUses, .prepared = std::move(*made)});
            return &preparedAnchors.back().prepared;
        }
        PreparedAnchor& oldest =
            *std::min_element(preparedAnchors.begin(), preparedAnchors.end(),
                              [](const PreparedAnchor& a, const PreparedAnchor& b) { return a.lastUsed < b.lastUsed; });
        oldest = PreparedAnchor{.anchor = anchorIndex, .lastUsed = anchorUses, .prepared = std::move(*made)};
        return &oldest.prepared;
    };

    // Whether a frame was registered through one anchor; a refusal of the attempt is an answer of false.
    const auto tryAnchor = [&](const std::size_t index, const std::size_t anchorIndex) -> an::Result<bool> {
        MatchingFrame& target = frames[index];
        const MatchingFrame& anchor = frames[anchorIndex];
        if (unpreparedAnchors.contains(anchorIndex))
        {
            return false;
        }
        const an::Result<const an::PreparedStarAlignmentReference*> prepared = preparedAnchor(anchorIndex);
        if (!prepared.hasValue())
        {
            if (prepared.status().code() == an::ErrorCode::Cancelled)
            {
                return prepared.status();
            }
            LOG_DEBUG("Image Registration: '{}' cannot anchor '{}': {}", anchor.path->toStdString(),
                      target.path->toStdString(), prepared.status().message());
            unpreparedAnchors.insert(anchorIndex);
            return false;
        }
        const an::Result<an::StarAlignmentResult> matched = an::alignStarFields(execution, target.points, **prepared);
        if (!matched.hasValue())
        {
            if (matched.status().code() == an::ErrorCode::Cancelled)
            {
                return matched.status();
            }
            LOG_DEBUG("Image Registration: '{}' did not match '{}': {}", target.path->toStdString(),
                      anchor.path->toStdString(), matched.status().message());
            return false;
        }
        if (const an::Status held = checkRegistrationStarMatch(matched->quality, model.minimumMatches,
                                                               kRegistrationSmallMatchInlierFraction);
            held.failed())
        {
            LOG_DEBUG("Image Registration: '{}' did not match '{}': {}", target.path->toStdString(),
                      anchor.path->toStdString(), held.message());
            return false;
        }
        // The anchor's matched stars carried onto the reference through its transform.
        std::vector<an::PixelCoordinate> source;
        std::vector<an::PixelCoordinate> carried;
        source.reserve(matched->matches.size());
        carried.reserve(matched->matches.size());
        for (const an::StarMatch& match : matched->matches)
        {
            const an::StarAlignmentPoint& anchorStar = anchor.points[match.referenceIndex];
            const an::Result<an::PixelCoordinate> onReference = an::mapRegistrationPoint(
                *anchor.frame->transform, an::PixelCoordinate{.x = anchorStar.x, .y = anchorStar.y});
            if (!onReference.hasValue())
            {
                continue;
            }
            const an::StarAlignmentPoint& star = target.points[match.sourceIndex];
            source.push_back(an::PixelCoordinate{.x = star.x, .y = star.y});
            carried.push_back(*onReference);
        }
        if (source.size() < static_cast<std::size_t>(requiredPairs))
        {
            return false;
        }
        // A stated order the carried pairs cannot carry - too few of them, or a fit the order refuses - gives way to
        // the linear fit over them, as a linear model registers the frame through this anchor, and says so.
        an::RegistrationFitRequest anchoredFit = fit;
        QString note;
        const bool statedOrder = fit.polynomialPolicy == an::RegistrationPolynomialPolicy::StatedOrder;
        if (statedOrder && source.size() < an::registrationPolynomialTermCount(fit.polynomialOrder))
        {
            anchoredFit.polynomialPolicy = an::RegistrationPolynomialPolicy::LinearOnly;
            note = linearInsteadOfOrderNote(source.size(), fit.polynomialOrder, *anchor.path, nullptr);
        }
        an::Result<an::RegistrationFitResult> fitted =
            an::fitRegistrationTransform(execution, source, carried, anchoredFit);
        if (!fitted.hasValue() && statedOrder && note.isEmpty() && !stopsTheFit(fitted.status()))
        {
            note =
                linearInsteadOfOrderNote(source.size(), fit.polynomialOrder, *anchor.path, fitted.status().message());
            anchoredFit.polynomialPolicy = an::RegistrationPolynomialPolicy::LinearOnly;
            fitted = an::fitRegistrationTransform(execution, source, carried, anchoredFit);
        }
        if (!fitted.hasValue())
        {
            if (fitted.status().code() == an::ErrorCode::Cancelled)
            {
                return fitted.status();
            }
            LOG_DEBUG("Image Registration: the fit of '{}' through '{}' failed: {}", target.path->toStdString(),
                      anchor.path->toStdString(), fitted.status().message());
            return false;
        }
        RegisteredFrame& frame = *target.frame;
        frame.transform = fitted->transform;
        frame.failed = false;
        frame.metrics.succeeded = true;
        frame.metrics.astrometric_rms_pixels = fitted->rootMeanSquareResidual;
        frame.registrationNote = note;
        LOG_INFO("Image Registration: '{}' registered through '{}' ({} star pairs, {:.3f} px residual)",
                 target.path->toStdString(), anchor.path->toStdString(), source.size(), fitted->rootMeanSquareResidual);
        if (!note.isEmpty())
        {
            LOG_WARNING("Image Registration: '{}' {}", target.path->toStdString(), note.toStdString());
        }
        return true;
    };

    // A refusal is deterministic - neither frame's stars nor the anchor's transform change during the pass - so a pair
    // refused once still counts among the frame's attempts, but is never tried again.
    std::set<std::pair<std::size_t, std::size_t>> refusedPairs;
    std::size_t recovered = 0u;
    bool progressed = true;
    while (!unregistered.empty() && progressed)
    {
        if (an::Status checked = execution.checkCancellation(); checked.failed())
        {
            return checked;
        }
        progressed = false;
        // The frames nearest a registered frame first, and the earlier of two at one distance.
        std::vector<std::size_t> distances(count, count);
        for (const std::size_t index : unregistered)
        {
            distances[index] = nearestAnchorDistance(index);
        }
        std::sort(unregistered.begin(), unregistered.end(), [&distances](const std::size_t a, const std::size_t b) {
            return distances[a] != distances[b] ? distances[a] < distances[b] : a < b;
        });
        std::vector<std::size_t> remaining;
        for (const std::size_t index : unregistered)
        {
            bool registered = false;
            if (holdsEnoughStars(index))
            {
                std::size_t attempts = 0u;
                for (std::size_t distance = 1u;
                     distance < count && attempts < kMaximumRegistrationAnchorAttempts && !registered; ++distance)
                {
                    if (an::Status checked = execution.checkCancellation(); checked.failed())
                    {
                        return checked;
                    }
                    for (const bool before : {true, false})
                    {
                        if (registered || (before && index < distance) || (!before && index + distance >= count))
                        {
                            continue;
                        }
                        const std::size_t anchorIndex = before ? index - distance : index + distance;
                        if (!registeredAnchor(anchorIndex))
                        {
                            continue;
                        }
                        ++attempts;
                        if (refusedPairs.contains({index, anchorIndex}))
                        {
                            continue;
                        }
                        const an::Result<bool> tried = tryAnchor(index, anchorIndex);
                        if (!tried.hasValue())
                        {
                            return tried.status();
                        }
                        registered = *tried;
                        if (!registered)
                        {
                            refusedPairs.insert({index, anchorIndex});
                        }
                    }
                }
            }
            if (registered)
            {
                progressed = true;
                ++recovered;
            }
            else
            {
                remaining.push_back(index);
            }
        }
        unregistered = std::move(remaining);
    }
    if (recovered > 0u)
    {
        LOG_INFO("Image Registration: {} frame(s) registered through a frame near them, {} still unregistered",
                 recovered, unregistered.size());
    }
    return an::Status::success();
}

} // namespace

an::StarDetectionRequest registrationStarDetection(const RegistrationSettings& settings) noexcept
{
    an::StarDetectionRequest request;
    request.searchRadius = detectorRadiusOf(settings);
    request.noiseSigma = settings.threshold;
    request.minimumRoundness = detectorRoundnessOf(settings);
    request.maximumRoundness = settings.roundnessRangeEnabled
                                   ? std::clamp(settings.roundnessThresholdMaximum, request.minimumRoundness, 1.0)
                                   : 1.0;
    request.model = fitsMoffatProfiles(settings) ? an::PsfModel::MoffatFreeBeta : an::PsfModel::Gaussian;
    request.minimumBeta = detectorBetaOf(settings);
    // Both bounds zero is no amplitude window, the rule for an unchecked range.
    request.minimumAmplitude = settings.amplitudeRangeEnabled ? (std::max)(0.0, settings.thresholdMinimum) : 0.0;
    request.maximumAmplitude =
        settings.amplitudeRangeEnabled ? (std::max)(request.minimumAmplitude, settings.thresholdMaximum) : 0.0;
    request.convergence = static_cast<std::uint32_t>((std::max)(settings.convergence, 1));
    request.relaxedChecks = settings.relaxPsfChecks;
    if (settings.maxStarPairs > 0)
    {
        request.maximumStars = static_cast<std::uint32_t>(settings.maxStarPairs);
    }
    request.measureRobustScale = true;
    request.fullScale = 1.0;
    return request;
}

QString registrationStarDetectionAdjustment(const RegistrationSettings& settings)
{
    QStringList adjusted;
    if (const std::uint32_t radius = detectorRadiusOf(settings); static_cast<int>(radius) != settings.radius)
    {
        adjusted << QStringLiteral("radius %1 as %2").arg(settings.radius).arg(radius);
    }
    if (const double roundness = detectorRoundnessOf(settings); roundness != settings.roundnessThreshold)
    {
        adjusted << QStringLiteral("roundness threshold %1 as %2")
                        .arg(settings.roundnessThreshold, 0, 'f', 2)
                        .arg(roundness, 0, 'f', 2);
    }
    if (const double beta = detectorBetaOf(settings); fitsMoffatProfiles(settings) && beta != settings.minimumBeta)
    {
        adjusted << QStringLiteral("minimum beta %1 as %2").arg(settings.minimumBeta, 0, 'f', 2).arg(beta, 0, 'f', 2);
    }
    if (adjusted.isEmpty())
    {
        return {};
    }
    return QStringLiteral("Star detection settings the detector refuses run at the nearest value it takes: %1.")
        .arg(adjusted.join(QStringLiteral(", ")));
}

RegistrationDemosaic registrationDemosaicOf(const RegistrationSettings& settings)
{
    RegistrationDemosaic demosaic;
    const ImageIntegration::PersistedChoiceValue method =
        ImageIntegration::persistedChoiceFromText(PersistedChoice::DemosaicMethod, settings.debayerMethod);
    // "Auto" and the setting's default read as the one demosaic setting's method.
    demosaic.method = ImageIntegration::demosaicMethodForKey(method.key).value_or(an::ui::demosaicMethodFromSettings());
    demosaic.pattern = an::ui::bayerPatternChoiceFromText(
        settings.cfaPattern.trimmed().compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0
            ? an::ui::kBayerPatternFromImageText
            : settings.cfaPattern.trimmed().toUpper());
    return demosaic;
}

an::Result<RegistrationModel> registrationModelOf(const RegistrationSettings& settings)
{
    RegistrationModel model;
    model.minimumMatches = static_cast<std::uint32_t>((std::max)(settings.minStarPairs, 3));
    const QString method =
        ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(settings.registrationMethod);
    if (method == QStringLiteral("phase_correlation"))
    {
        model.phaseCorrelation = true;
        model.starModel = an::StarTransformModel::Similarity;
        model.fit.linearKind = an::RegistrationTransformKind::Similarity;
        return model;
    }
    if (method != QStringLiteral("star_matching") && method != QStringLiteral("comet_tracking"))
    {
        return refusal(an::ErrorCode::InvalidArgument,
                       "Unknown registration method '" + settings.registrationMethod.toStdString() + "'");
    }
    const QString mode = settings.transformMode.trimmed().toLower();
    if (mode == QStringLiteral("polynomial") || mode == QStringLiteral("auto"))
    {
        model.starModel = an::StarTransformModel::Projective;
        model.fit.linearKind = an::RegistrationTransformKind::Projective;
        if (mode == QStringLiteral("auto"))
        {
            model.fit.polynomialPolicy = an::RegistrationPolynomialPolicy::AutomaticOrder;
        }
        else
        {
            if (settings.transformOrder < static_cast<int>(an::kMinimumRegistrationPolynomialOrder) ||
                settings.transformOrder > static_cast<int>(an::kMaximumRegistrationPolynomialOrder))
            {
                return refusal(an::ErrorCode::InvalidArgument, "A polynomial transform's order is two, three or four");
            }
            model.fit.polynomialPolicy = an::RegistrationPolynomialPolicy::StatedOrder;
            model.fit.polynomialOrder = static_cast<std::uint32_t>(settings.transformOrder);
        }
        return model;
    }
    const auto linear = linearModelOf(settings.transformation);
    if (!linear.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument,
                       "Unknown transformation '" + settings.transformation.toStdString() + "'");
    }
    model.starModel = linear->first;
    model.fit.linearKind = linear->second;
    return model;
}

an::RegistrationFitRequest manualRegistrationFit(const RegistrationSettings& settings, const std::uint32_t width,
                                                 const std::uint32_t height)
{
    an::RegistrationFitRequest request;
    const QString transformation = settings.transformation.trimmed().toLower();
    if (transformation == QStringLiteral("similarity") || transformation == QStringLiteral("translation"))
    {
        request.linearKind = an::RegistrationTransformKind::Similarity;
    }
    else if (transformation == QStringLiteral("affine"))
    {
        request.linearKind = an::RegistrationTransformKind::Affine;
    }
    else
    {
        request.linearKind = an::RegistrationTransformKind::Projective;
    }
    // The mode decides, as registrationModelOf reads it: an order beside a linear mode is not a polynomial.
    const QString mode = settings.transformMode.trimmed().toLower();
    if (mode == QStringLiteral("auto"))
    {
        request.polynomialPolicy = an::RegistrationPolynomialPolicy::AutomaticOrder;
    }
    else if (mode == QStringLiteral("polynomial") &&
             settings.transformOrder >= static_cast<int>(an::kMinimumRegistrationPolynomialOrder) &&
             settings.transformOrder <= static_cast<int>(an::kMaximumRegistrationPolynomialOrder))
    {
        request.polynomialPolicy = an::RegistrationPolynomialPolicy::StatedOrder;
        request.polynomialOrder = static_cast<std::uint32_t>(settings.transformOrder);
    }
    const double half_width = static_cast<double>((std::max)(width, 1u)) * 0.5;
    const double half_height = static_cast<double>((std::max)(height, 1u)) * 0.5;
    request.normalizationOrigin = an::PixelCoordinate{.x = half_width, .y = half_height};
    request.normalizationScale =
        an::PixelCoordinate{.x = (std::max)(half_width, 1.0), .y = (std::max)(half_height, 1.0)};
    return request;
}

an::Result<an::Image<float>> readRegistrationFrame(const an::consumer::EngineSession& session, const QString& path,
                                                   const RegistrationDemosaic& demosaic, std::stop_token stop)
{
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, an::ui::ViewReadRequest{.path = path}, stop, nullptr);
    if (!read.hasValue())
    {
        return read.status();
    }
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::Image<float>> widened = fractionsOf(*scope, lease->image());
    if (!widened.hasValue())
    {
        return widened.status();
    }
    const std::optional<an::ImageDescriptor> described = widened->descriptor();
    if (!described.has_value() || !described->isMosaiced())
    {
        return widened;
    }
    const an::Result<an::CfaDescriptor> pattern = an::ui::resolveBayerPattern(demosaic.pattern, *described);
    if (!pattern.hasValue())
    {
        return pattern.status();
    }
    an::Result<an::consumer::AnyImage> colour = an::ui::demosaicViewFrame(
        scope->engine(), scope->execution(), an::consumer::AnyImage(std::move(*widened)), demosaic.method, *pattern);
    if (!colour.hasValue())
    {
        return colour.status();
    }
    if (auto* typed = std::get_if<an::Image<float>>(&*colour))
    {
        return std::move(*typed);
    }
    return refusal(an::ErrorCode::Internal, "the demosaiced frame is not Float32");
}

namespace
{

/// The record of a frame's detection.
[[nodiscard]] FrameDetection registrationDetectionOf(an::StarDetectionResult&& detected);

} // namespace

an::Result<FrameDetection> detectRegistrationStars(const an::ExecutionContext& execution, const an::Image<float>& frame,
                                                   const an::StarDetectionRequest& request)
{
    an::Result<an::StarDetectionResult> detected = detectChannel(execution, frame, request, searchChannelOf(frame));
    if (!detected.hasValue())
    {
        return detected.status();
    }
    return registrationDetectionOf(std::move(*detected));
}

an::Result<an::StarChannelStatistics> measureRegistrationChannel(const an::ExecutionContext& execution,
                                                                 const an::Image<float>& frame,
                                                                 const an::StarDetectionRequest& request)
{
    const auto view = frame.view();
    const std::optional<an::ImageDescriptor> descriptor = frame.descriptor();
    if (!view.has_value() || !descriptor.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    // The frame view borrows its layout, so the layout it is given outlives the measurement.
    const an::ImageLayout layout = view->layout();
    const std::uint32_t channel = searchChannelOf(frame);
    const an::Result<an::StarFrameView> starFrame = starFrameOf(frame, layout, channel);
    if (!starFrame.hasValue())
    {
        return starFrame.status();
    }
    const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(request, *descriptor, layout, channel);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    return an::measureStarChannel(execution, *starFrame, plan->channelIndex(), plan->region());
}

an::Result<FrameDetection> detectRegistrationStars(const an::ExecutionContext& execution, const an::Image<float>& frame,
                                                   const an::StarDetectionRequest& request,
                                                   const an::StarChannelStatistics& measured)
{
    an::Result<an::StarDetectionResult> detected =
        detectChannel(execution, frame, request, searchChannelOf(frame), &measured);
    if (!detected.hasValue())
    {
        return detected.status();
    }
    return registrationDetectionOf(std::move(*detected));
}

namespace
{

FrameDetection registrationDetectionOf(an::StarDetectionResult&& detected)
{
    FrameDetection detection;
    RegisteredFrame& record = detection.frame;
    record.stars.reserve(detected.stars.size());
    for (const an::DetectedStar& star : detected.stars)
    {
        record.stars.push_back(RegisteredStar{.x = star.positionX,
                                              .y = star.positionY,
                                              .fwhm = star.metrics.meanFullWidth,
                                              .roundness = star.metrics.roundness});
    }
    const an::StarFieldSummary& summary = detected.summary;
    record.metrics.stars = static_cast<int>(detected.stars.size());
    record.metrics.fwhm = summary.medianFullWidth.value_or(0.0);
    record.metrics.snr = summary.medianSignalToNoise.value_or(0.0);
    record.metrics.eccentricity = registrationEccentricityFromRoundness(summary.medianRoundness.value_or(0.0));
    record.metrics.succeeded = true;
    record.quality = summary.quality.value_or(0.0);
    record.noise.background_noise = summary.channel.backgroundNoise;
    record.noise.global_mad = summary.channel.medianAbsoluteDeviation.value_or(0.0);
    record.noise.global_scale = summary.channel.robustScale.value_or(0.0);
    detection.stars = std::move(detected.stars);
    return detection;
}

} // namespace

an::Result<ChromaticCorrection>
correctChromaticAberration(const an::EngineContext& engine, const an::ExecutionContext& execution,
                           an::Image<float>& frame, const RegistrationSettings::ChromaticAberrationSettings& settings,
                           const an::StarDetectionRequest& detection)
{
    ChromaticCorrection outcome;
    const std::optional<an::ImageDescriptor> descriptor = frame.descriptor();
    if (!descriptor.has_value() || descriptor->extent().channelCount() != 3u)
    {
        return outcome;
    }
    const std::uint32_t width = descriptor->extent().width();
    const std::uint32_t height = descriptor->extent().height();
    an::StarDetectionRequest request = detection;
    request.maximumStars = kChromaticAberrationMaximumStars;
    std::array<std::vector<an::StarAlignmentPoint>, 3u> points;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        an::Result<an::StarDetectionResult> found = detectChannel(execution, frame, request, channel);
        if (!found.hasValue())
        {
            return found.status();
        }
        points[channel] = edgeStars(found->stars, width, height, settings.edgeFraction);
    }
    const auto linear = linearModelOf(settings.transformType);
    const an::StarTransformModel model = linear.has_value() ? linear->first : an::StarTransformModel::Similarity;
    const an::Result<an::KernelInterpolation> interpolation = alignmentInterpolationOf(settings.interpolation);

    std::optional<an::Image<float>> corrected;
    for (const std::uint32_t channel : {0u, 2u})
    {
        bool& applied = channel == 0u ? outcome.redApplied : outcome.blueApplied;
        std::uint32_t& matches = channel == 0u ? outcome.redMatches : outcome.blueMatches;
        double& residual = channel == 0u ? outcome.redResidual : outcome.blueResidual;
        const char* name = channel == 0u ? "red" : "blue";
        if (points[channel].size() < kChromaticAberrationMinimumPoints ||
            points[1].size() < kChromaticAberrationMinimumPoints)
        {
            LOG_WARNING("Chromatic aberration: the {} channel has too few edge stars ({} beside {} green)", name,
                        points[channel].size(), points[1].size());
            continue;
        }
        const std::uint32_t minimumPairs = static_cast<std::uint32_t>((std::max)(settings.minMatchedStars, 0));
        const an::Result<an::StarAlignmentResult> matched =
            an::alignStarFields(execution, points[channel], points[1], starMatchRequest(model, minimumPairs));
        if (!matched.hasValue())
        {
            if (matched.status().code() == an::ErrorCode::Cancelled)
            {
                return matched.status();
            }
            LOG_WARNING("Chromatic aberration: the {} channel matched no stars: {}", name, matched.status().message());
            continue;
        }
        matches = matched->quality.inlierMatches;
        residual = matched->quality.rootMeanSquareResidual;
        if (const an::Status held = checkRegistrationStarMatch(matched->quality, minimumPairs,
                                                               kChromaticAberrationSmallMatchInlierFraction);
            held.failed() || !std::isfinite(residual))
        {
            LOG_WARNING("Chromatic aberration: the {} channel's match does not hold: {}", name,
                        held.failed() ? held.message() : "its residual is not finite");
            continue;
        }
        const an::Result<an::RegistrationTransform> transform = an::registrationTransformOf(matched->transform);
        if (!transform.hasValue())
        {
            continue;
        }
        an::Result<Warped> warped = warpOnto(
            engine, execution, frame, *transform, an::SipDistortion{},
            an::RegistrationWarpRequest{
                .width = width,
                .height = height,
                .sampling = an::SamplingKernelParameters{
                    .interpolation = interpolation.hasValue() ? *interpolation : an::KernelInterpolation::Lanczos4,
                    .border = an::KernelBorderMode::Constant,
                    .invalid = an::SamplingInvalidPolicy::RequireAll}});
        if (!warped.hasValue())
        {
            return warped.status();
        }
        if (!corrected.has_value())
        {
            an::Result<an::Image<float>> copy = frame.clone();
            if (!copy.hasValue())
            {
                return copy.status();
            }
            corrected.emplace(std::move(*copy));
        }
        // Only the channel moves: its plane of the warped frame replaces the corrected frame's.
        const auto from = std::as_const(warped->image).view();
        std::optional<an::ImageWriteAccess<float>> access = corrected->write();
        if (!from.has_value() || !access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "a corrected channel holds nothing");
        }
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const std::optional<std::span<const float>> in = from->row(channel, y);
            const std::optional<std::span<float>> out = access->view().row(channel, y);
            if (in.has_value() && out.has_value())
            {
                std::copy(in->begin(), in->end(), out->begin());
            }
        }
        applied = true;
        LOG_INFO("Chromatic aberration: {} channel moved onto green, {} stars matched, {:.3f} px residual", name,
                 matches, residual);
    }
    if (corrected.has_value())
    {
        frame = std::move(*corrected);
    }
    return outcome;
}

an::Status checkRegistrationStarMatch(const an::StarAlignmentQuality& quality, const std::uint32_t minimumPairs,
                                      const double smallMatchInlierFraction)
{
    const std::uint32_t required = (std::max)(minimumPairs, kReliableMinimumStarPairs);
    if (quality.inlierMatches < required)
    {
        return refusal(an::ErrorCode::EmptySelection, std::to_string(quality.inlierMatches) + " star pairs matched, " +
                                                          std::to_string(required) + " needed");
    }
    // Among a few pairs, a few spurious ones can fit one transform, so most of a small match's candidates must fit it;
    // a crowded field gives a match more candidates that fit nothing, and is exempt.
    const bool small = quality.candidateMatches < kSmallStarMatchCandidates;
    const bool crowded =
        quality.sourcePoints >= kCrowdedStarFieldStars || quality.referencePoints >= kCrowdedStarFieldStars;
    if (small && !crowded && quality.inlierFraction < smallMatchInlierFraction)
    {
        return refusal(an::ErrorCode::EmptySelection,
                       std::to_string(quality.inlierMatches) + " of the " + std::to_string(quality.candidateMatches) +
                           " candidate star pairs fit the transform, under the " +
                           std::to_string(std::lround(smallMatchInlierFraction * 100.0)) + "% a match of fewer than " +
                           std::to_string(kSmallStarMatchCandidates) + " pairs needs");
    }
    return an::Status::success();
}

std::optional<an::SipDistortion> registrationDistortionOf(const an::WcsSolution& solution)
{
    if (!correctsPositions(solution.distortion()))
    {
        return std::nullopt;
    }
    return solution.distortion();
}

an::KernelValidityView registrationChannelValidity(const an::Image<float>& frame, const std::uint32_t channel)
{
    return holdsNonFiniteSample(frame, channel) ? an::KernelValidityView{.descriptor = kFiniteSamples}
                                                : an::KernelValidityView{};
}

an::Status matchRegistrationStars(const an::ExecutionContext& execution, RegisteredFrameSet& set,
                                  const QString& referencePath, const RegistrationModel& model,
                                  const std::uint32_t width, const std::uint32_t height,
                                  const std::function<void(std::size_t done, std::size_t total)>& progress)
{
    if (!set.frames.contains(referencePath))
    {
        return refusal(an::ErrorCode::InvalidArgument,
                       "The reference frame '" + referencePath.toStdString() + "' was not detected");
    }
    // Every frame in the set's order, with the positions its stars are matched at.
    std::vector<MatchingFrame> frames;
    frames.reserve(set.frames.size());
    std::size_t referenceIndex = 0u;
    for (auto& [path, frame] : set.frames)
    {
        if (path == referencePath)
        {
            referenceIndex = frames.size();
        }
        frames.push_back(MatchingFrame{.path = &path, .frame = &frame, .points = matchingPointsOf(frame)});
    }
    const std::vector<an::StarAlignmentPoint>& referencePoints = frames[referenceIndex].points;
    const an::Result<an::PreparedStarAlignmentReference> prepared = an::prepareStarAlignmentReference(
        execution, referencePoints, starMatchRequest(model.starModel, model.minimumMatches));
    if (!prepared.hasValue())
    {
        if (prepared.status().code() == an::ErrorCode::Cancelled)
        {
            return prepared.status();
        }
        return an::Status::failure(prepared.status().code(),
                                   an::StatusDetail{.message = "The reference frame holds too few stars to match"},
                                   prepared.status());
    }
    set.referencePath = referencePath;
    const double halfWidth = static_cast<double>(width) * 0.5;
    const double halfHeight = static_cast<double>(height) * 0.5;
    an::RegistrationFitRequest fit = model.fit;
    fit.normalizationOrigin = an::PixelCoordinate{.x = halfWidth, .y = halfHeight};
    fit.normalizationScale = an::PixelCoordinate{.x = (std::max)(halfWidth, 1.0), .y = (std::max)(halfHeight, 1.0)};
    std::size_t done = 0u;
    for (MatchingFrame& matching : frames)
    {
        if (an::Status checked = execution.checkCancellation(); checked.failed())
        {
            return checked;
        }
        RegisteredFrame& frame = *matching.frame;
        const QString& path = *matching.path;
        frame.registrationNote.clear();
        const auto fail = [&frame](const std::string& why, const QString& name) {
            frame.failed = true;
            frame.transform.reset();
            frame.metrics.succeeded = false;
            LOG_WARNING("Image Registration: '{}' was not registered: {}", name.toStdString(), why);
        };
        if (path == referencePath)
        {
            frame.transform = an::RegistrationTransform{};
            frame.failed = false;
            frame.metrics.succeeded = true;
            frame.metrics.astrometric_rms_pixels = 0.0;
        }
        else
        {
            const std::vector<an::StarAlignmentPoint>& points = matching.points;
            const an::Result<an::StarAlignmentResult> matched = an::alignStarFields(execution, points, *prepared);
            if (!matched.hasValue())
            {
                if (matched.status().code() == an::ErrorCode::Cancelled)
                {
                    return matched.status();
                }
                fail(matched.status().message(), path);
            }
            else if (const an::Status held = checkRegistrationStarMatch(matched->quality, model.minimumMatches,
                                                                        kRegistrationSmallMatchInlierFraction);
                     held.failed())
            {
                fail(held.message(), path);
            }
            else if (model.fit.polynomialPolicy != an::RegistrationPolynomialPolicy::LinearOnly)
            {
                std::vector<an::PixelCoordinate> source;
                std::vector<an::PixelCoordinate> target;
                for (const an::StarMatch& match : matched->matches)
                {
                    source.push_back(
                        an::PixelCoordinate{.x = points[match.sourceIndex].x, .y = points[match.sourceIndex].y});
                    target.push_back(an::PixelCoordinate{.x = referencePoints[match.referenceIndex].x,
                                                         .y = referencePoints[match.referenceIndex].y});
                }
                // A stated order needs as many pairs as it has terms. A frame whose pairs cannot carry it - too few of
                // them, or a fit the order refuses - keeps the linear transform its stars matched, as a linear model
                // registers it, and says so.
                const auto keepLinear = [&](const char* refusal) {
                    const an::Result<an::RegistrationTransform> transform =
                        an::registrationTransformOf(matched->transform);
                    if (!transform.hasValue())
                    {
                        fail(transform.status().message(), path);
                        return;
                    }
                    frame.transform = *transform;
                    frame.failed = false;
                    frame.metrics.succeeded = true;
                    frame.metrics.astrometric_rms_pixels = matched->quality.rootMeanSquareResidual;
                    frame.registrationNote =
                        linearInsteadOfOrderNote(source.size(), model.fit.polynomialOrder, QString(), refusal);
                    LOG_WARNING("Image Registration: '{}' {}", path.toStdString(),
                                frame.registrationNote.toStdString());
                };
                const bool statedOrder = model.fit.polynomialPolicy == an::RegistrationPolynomialPolicy::StatedOrder;
                if (statedOrder && source.size() < an::registrationPolynomialTermCount(model.fit.polynomialOrder))
                {
                    keepLinear(nullptr);
                }
                else if (const an::Result<an::RegistrationFitResult> fitted =
                             an::fitRegistrationTransform(execution, source, target, fit);
                         !fitted.hasValue())
                {
                    if (fitted.status().code() == an::ErrorCode::Cancelled)
                    {
                        return fitted.status();
                    }
                    if (statedOrder && !stopsTheFit(fitted.status()))
                    {
                        keepLinear(fitted.status().message());
                    }
                    else
                    {
                        fail(fitted.status().message(), path);
                    }
                }
                else
                {
                    frame.transform = fitted->transform;
                    frame.failed = false;
                    frame.metrics.succeeded = true;
                    frame.metrics.astrometric_rms_pixels = fitted->rootMeanSquareResidual;
                }
            }
            else
            {
                const an::Result<an::RegistrationTransform> transform = an::registrationTransformOf(matched->transform);
                if (!transform.hasValue())
                {
                    fail(transform.status().message(), path);
                }
                else
                {
                    frame.transform = *transform;
                    frame.failed = false;
                    frame.metrics.succeeded = true;
                    frame.metrics.astrometric_rms_pixels = matched->quality.rootMeanSquareResidual;
                }
            }
        }
        ++done;
        if (progress)
        {
            progress(done, set.frames.size());
        }
    }
    // A frame the reference holds too little of may still overlap a frame registered near it.
    return registerThroughNeighbours(execution, frames, referenceIndex, model, fit);
}

an::Result<an::RegistrationTransform> phaseCorrelationTransform(const an::ExecutionContext& execution,
                                                                const an::Image<float>& reference,
                                                                const an::Image<float>& frame)
{
    const auto referenceView = reference.view();
    const auto frameView = frame.view();
    if (!referenceView.has_value() || !frameView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a frame holds nothing");
    }
    const auto referencePlane = referenceView->plane(searchChannelOf(reference));
    const auto framePlane = frameView->plane(searchChannelOf(frame));
    if (!referencePlane.has_value() || !framePlane.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "a frame has no plane to correlate");
    }
    const an::Result<an::PhaseCorrelationResult> measured =
        an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, referencePlane->data(),
                                         referencePlane->layout(), framePlane->data(), framePlane->layout());
    if (!measured.hasValue())
    {
        return measured.status();
    }
    return an::RegistrationTransform::create(an::RegistrationTransformRequest{
        .linearKind = an::RegistrationTransformKind::Similarity,
        .sourceToReference = {1.0, 0.0, measured->translationX, 0.0, 1.0, measured->translationY, 0.0, 0.0, 1.0}});
}

an::Result<bool> enrichRegisteredFrame(const an::ExecutionContext& execution, RegisteredFrame& frame,
                                       const std::span<const an::DetectedStar> stars, const an::WcsSolution& solution,
                                       const QString& catalogDirectory,
                                       const std::optional<double> observationEpochYear,
                                       std::vector<RegisteredCatalogMatch>* matches)
{
    if (matches != nullptr)
    {
        matches->clear();
    }
    an::Result<ProfileEnrichment> enriched = enrichProfileStars(
        execution, stars, solution,
        ProfileEnrichmentRequest{.catalogDirectory = catalogDirectory, .observationEpochYear = observationEpochYear});
    if (!enriched.hasValue())
    {
        return enriched.status();
    }
    const double scale =
        0.5 * (std::abs(solution.pixelScaleXArcsecPerPixel()) + std::abs(solution.pixelScaleYArcsecPerPixel()));
    if (std::isfinite(scale) && scale > 0.0 && frame.metrics.astrometric_rms_pixels > 0.0)
    {
        frame.metrics.astrometric_rms_arcsec = frame.metrics.astrometric_rms_pixels * scale;
    }
    if (enriched->catalogFailure.has_value())
    {
        LOG_WARNING("Image Registration: the Gaia catalog matched nothing: {}", enriched->catalogFailure->message());
        return false;
    }
    const ProfileCatalogFigures& figures = enriched->figures;
    frame.metrics.photometric_zero_point = figures.zeroPointG.value_or(0.0);
    frame.metrics.zero_point_scatter = figures.zeroPointError.value_or(0.0);
    frame.metrics.limiting_magnitude = figures.limitingMagnitude.value_or(0.0);
    frame.metrics.catalog_matched_stars = static_cast<int>(figures.matchedCount);
    if (matches != nullptr)
    {
        for (const StarEnrichment& star : enriched->stars)
        {
            if (star.match.has_value())
            {
                matches->push_back(RegisteredCatalogMatch{.identifier = star.match->identifier,
                                                          .separationArcseconds = star.match->separationArcseconds});
            }
        }
    }
    return figures.matchedCount > 0u;
}

an::RegistrationCanvasMode registrationCanvasModeOf(const QString& mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("mosaic"))
    {
        return an::RegistrationCanvasMode::Union;
    }
    if (normalized == QStringLiteral("intersection"))
    {
        return an::RegistrationCanvasMode::Intersection;
    }
    return an::RegistrationCanvasMode::Reference;
}

an::Result<an::KernelInterpolation> alignmentInterpolationOf(const QString& text)
{
    const ImageIntegration::PersistedChoiceValue choice =
        ImageIntegration::persistedChoiceFromText(PersistedChoice::Interpolation, text);
    if (!choice.known)
    {
        return refusal(an::ErrorCode::InvalidArgument, "Unknown interpolation '" + text.toStdString() + "'");
    }
    return ImageIntegration::interpolationForKey(choice.key).value_or(an::KernelInterpolation::Lanczos4);
}

QString alignedOutputPath(const QString& source, const QString& directory)
{
    return QDir(directory).filePath(QFileInfo(source).completeBaseName() + QStringLiteral("_reg.fits"));
}

an::Result<an::RegistrationCanvas> planAlignmentCanvas(const std::span<const an::RegistrationTransform> frames,
                                                       const std::uint32_t referenceIndex, const std::uint32_t width,
                                                       const std::uint32_t height,
                                                       const an::RegistrationCanvasMode mode,
                                                       const std::span<const an::SipDistortion> distortions)
{
    if (!distortions.empty() && distortions.size() != frames.size())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "every aligned frame states its distortion or none does");
    }
    std::vector<an::RegistrationBounds> bounds;
    bounds.reserve(frames.size());
    for (std::size_t index = 0u; index < frames.size(); ++index)
    {
        const an::SipDistortion* distortion =
            !distortions.empty() && correctsPositions(distortions[index]) ? &distortions[index] : nullptr;
        const an::Result<an::RegistrationBounds> bounded =
            an::computeRegistrationBounds(frames[index], width, height, an::PixelCoordinate{}, distortion);
        if (!bounded.hasValue())
        {
            return bounded.status();
        }
        bounds.push_back(*bounded);
    }
    return an::planRegistrationCanvas(bounds, referenceIndex, mode);
}

an::Status alignRegisteredFrame(const an::consumer::EngineSession& session, const QString& path,
                                const an::RegistrationTransform& transform, const an::SipDistortion& distortion,
                                const an::RegistrationCanvas& canvas, const an::KernelInterpolation interpolation,
                                const RegistrationDemosaic& demosaic, const QString& outputPath, std::stop_token stop)
{
    an::Result<an::Image<float>> frame = readRegistrationFrame(session, path, demosaic, stop);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    an::Result<an::consumer::ImageAsset> aligned = [&]() -> an::Result<an::consumer::ImageAsset> {
        an::Result<an::consumer::OperationScope> scope = session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<Warped> warped =
            warpOnto(scope->engine(), scope->execution(), *frame, transform,
                     correctsPositions(distortion) ? distortion : an::SipDistortion{},
                     an::RegistrationWarpRequest{
                         .width = canvas.width,
                         .height = canvas.height,
                         .canvasOrigin = canvas.origin,
                         .sampling = an::SamplingKernelParameters{.interpolation = interpolation,
                                                                  .border = an::KernelBorderMode::Constant,
                                                                  .invalid = an::SamplingInvalidPolicy::RequireAll}});
        if (!warped.hasValue())
        {
            return warped.status();
        }
        return an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(warped->image)),
                                               std::move(warped->mask));
    }();
    if (!aligned.hasValue())
    {
        return aligned.status();
    }
    const an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::Float32});
    if (!intent.hasValue())
    {
        return intent.status();
    }
    return an::ui::writeViewImage(session, *aligned, outputPath, *intent, stop);
}

} // namespace an::ui::processes
