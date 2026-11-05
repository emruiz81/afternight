// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - detecting stars channel by channel under a stated policy.
//
// Every policy channel is planned before any is detected in, so a request or a channel that cannot run
// is refused before a sample is read. Each attempt is one `detectStars` with its progress mapped into
// its own share of the operation's scale; the kept attempt is moved out, never copied, and an attempt
// that loses is released before the next one runs.

#include <AfterNightEngine/Algorithms/Stars/StarDetectionChannels.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "runtime/ProgressRange.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::detectStarsByChannelPolicy";

/// Progress units each attempt's share of the scale holds: one per stage of a detection.
constexpr std::uint64_t kUnitsPerAttempt = 4u;

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @return the complete failure.
[[nodiscard]] Status channelFailure(const ErrorCode code, std::string message)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

/// How many of an attempt's stars count.
///
/// @param stars the attempt's stars.
/// @param solution the frame's solution, or null to count every star.
/// @return the stars the solution places on the sky, or all of them.
[[nodiscard]] std::uint64_t countedStars(const std::vector<DetectedStar>& stars, const WcsSolution* const solution)
{
    if (solution == nullptr)
    {
        return stars.size();
    }
    return static_cast<std::uint64_t>(std::count_if(stars.begin(), stars.end(), [solution](const DetectedStar& star) {
        return solution->pixelToWorld(PixelCoordinate{.x = star.positionX, .y = star.positionY}).hasValue();
    }));
}

} // namespace

StarDetectionChannelPolicy greenThenRedChannelPolicy(const ImageDescriptor& descriptor)
{
    StarDetectionChannelPolicy policy;
    if (descriptor.extent().channelCount() >= 2u)
    {
        policy.channels = {1u, 0u};
    }
    else
    {
        policy.channels = {0u};
    }
    policy.sufficientStars = kDefaultSufficientDetectionStars;
    return policy;
}

Result<StarDetectionChannelResult> detectStarsByChannelPolicy(const ExecutionContext& execution,
                                                              const StarFrameView& frame,
                                                              const StarDetectionRequest& request,
                                                              const StarDetectionChannelPolicy& policy,
                                                              const WcsSolution* const solution)
{
    if (!execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized);
    }
    if (!frame.valid())
    {
        return channelFailure(ErrorCode::InvalidArgument, "the frame view borrows no frame");
    }
    if (policy.channels.empty())
    {
        return channelFailure(ErrorCode::InvalidArgument, "a policy names at least one channel");
    }
    for (std::size_t index = 0u; index < policy.channels.size(); ++index)
    {
        if (std::find(policy.channels.begin() + static_cast<std::ptrdiff_t>(index) + 1, policy.channels.end(),
                      policy.channels[index]) != policy.channels.end())
        {
            return channelFailure(ErrorCode::InvalidArgument, "a policy names each channel once");
        }
    }
    if (solution != nullptr && (solution->imageWidth() != frame.descriptor().extent().width() ||
                                solution->imageHeight() != frame.descriptor().extent().height()))
    {
        return channelFailure(ErrorCode::IncompatibleArguments, "the solution describes a frame of another size");
    }

    std::vector<StarDetectionPlan> plans;
    plans.reserve(policy.channels.size());
    for (const std::uint32_t channel : policy.channels)
    {
        Result<StarDetectionPlan> plan = planStarDetection(request, frame.descriptor(), frame.layout(), channel);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        plans.push_back(*plan);
    }

    const std::uint64_t total = kUnitsPerAttempt * static_cast<std::uint64_t>(plans.size());
    StarDetectionChannelResult result;
    result.attempts.reserve(plans.size());
    bool kept = false;
    std::uint64_t keptCount = 0u;
    for (std::size_t index = 0u; index < plans.size(); ++index)
    {
        StarDetectionResult detection;
        {
            const ProgressRange share(execution, kUnitsPerAttempt * index, kUnitsPerAttempt * (index + 1u), total);
            if (const Status ran = detectStars(execution, frame, plans[index], detection); ran.failed())
            {
                return ran;
            }
        }
        const StarDetectionChannelAttempt attempt{.channel = plans[index].channelIndex(),
                                                  .detectedStars = detection.stars.size(),
                                                  .countedStars = countedStars(detection.stars, solution)};
        result.attempts.push_back(attempt);
        if (!kept || attempt.countedStars > keptCount)
        {
            result.detection = std::move(detection);
            result.channel = attempt.channel;
            keptCount = attempt.countedStars;
            kept = true;
        }
        if (keptCount >= policy.sufficientStars)
        {
            break;
        }
    }
    result.sufficient = keptCount >= policy.sufficientStars;
    execution.reportProgress(total, total, "star detection by channel policy");
    return result;
}

} // namespace an
