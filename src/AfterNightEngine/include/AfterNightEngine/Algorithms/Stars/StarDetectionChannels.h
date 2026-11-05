// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Detecting stars channel by channel until one channel gives enough, by a stated policy.
///
/// A colour frame's stars are clearest in one channel and sometimes missing from it: a narrowband
/// frame has nothing in green, a frame through a red filter little in blue. The viewer's astrometry
/// overlay looked for stars in green and, when green gave fewer than it needed, in red, keeping
/// whichever gave more. That loop lived in the viewer; this is the same decision as a policy a caller
/// states and the engine runs - which channels, in which order, and how many stars are enough to stop.
///
/// **What an attempt counts.** An attempt is one `detectStars` over one channel with the caller's
/// request. Given a world-coordinate solution of the frame, it counts the stars the solution places on
/// the sky - those `WcsSolution::pixelToWorld` answers for - which is what the overlay can draw;
/// without one, it counts every star found.
///
/// **When it stops, and what it keeps.** The channels are tried in the policy's order, and the search
/// stops after the first attempt that counts at least `StarDetectionChannelPolicy::sufficientStars`,
/// or after the last channel. The attempt kept is the one that counted the most, the earliest of equal
/// counts, so a frame with stars in no channel answers with its first channel's empty detection.

#pragma once

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <cstdint>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// Where one solved frame points. Defined in `Algorithms/Astrometry/WcsSolution.h`.
class WcsSolution;

/// How many stars an attempt counts before the overlay's policy stops trying further channels.
inline constexpr std::uint32_t kDefaultSufficientDetectionStars = 25u;

/// Which channels a detection tries, in which order, and how many stars end the search.
struct StarDetectionChannelPolicy
{
    /// The channels to detect in, in the order they are tried. Not empty, each below the frame's
    /// channel count, and none twice.
    std::vector<std::uint32_t> channels{};

    /// The fewest stars an attempt counts for the search to stop after it. Zero stops after the first
    /// attempt.
    std::uint32_t sufficientStars = kDefaultSufficientDetectionStars;
};

/// The overlay's policy for a frame of this description: green, then red.
///
/// Channel one and then channel zero for a frame of two or more channels - green and then red for an
/// RGB frame - and channel zero alone for a one-channel frame, with
/// `kDefaultSufficientDetectionStars`.
///
/// Thread-safe and reentrant. Allocates the channel list.
///
/// @param descriptor the frame the policy is for.
/// @return the policy.
[[nodiscard]] AFTERNIGHTENGINE_API StarDetectionChannelPolicy
greenThenRedChannelPolicy(const ImageDescriptor& descriptor);

/// What one channel's attempt found.
struct StarDetectionChannelAttempt
{
    /// The channel detected in.
    std::uint32_t channel = 0u;

    /// The stars the detection accepted.
    std::uint64_t detectedStars = 0u;

    /// The stars that counted: those the solution places on the sky, or every detected star without
    /// one.
    std::uint64_t countedStars = 0u;

    /// Whether two attempts found the same.
    ///
    /// @param left one attempt.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StarDetectionChannelAttempt& left,
                                                   const StarDetectionChannelAttempt& right) noexcept = default;
};

/// The attempt a policy kept, and every attempt it made.
struct StarDetectionChannelResult
{
    /// The kept attempt's stars, summary and report, exactly as `detectStars` returned them.
    StarDetectionResult detection{};

    /// The channel the kept attempt detected in.
    std::uint32_t channel = 0u;

    /// Every attempt, in the order they were made; the kept one among them.
    std::vector<StarDetectionChannelAttempt> attempts{};

    /// Whether the kept attempt counted at least the policy's sufficient number of stars.
    bool sufficient = false;
};

/// Detects stars in a policy's channels in turn and keeps the attempt that counted most.
///
/// Every channel is planned by `planStarDetection` with the caller's request before any is detected
/// in, so a request one channel would refuse fails the call before it reads a sample. Progress is
/// reported in four units per policy channel, each attempt's four stages mapped into its share; a
/// search that stops early reports the whole total as it finishes.
///
/// Thread-safe with respect to `execution`, `frame` and `solution`, which are only read. A failure of
/// any attempt - cancellation included - is returned, with no result.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked frame to search.
/// @param request the detection parameters every attempt uses.
/// @param policy the channels and the stop.
/// @param solution the frame's world-coordinate solution, borrowed for the call, or null to count every
///        detected star. It must describe a frame of this frame's width and height.
/// @return the kept attempt and every attempt; `ErrorCode::NotInitialized` when `execution` holds
///         nothing; `ErrorCode::InvalidArgument` for a default frame view, or a policy with no channel
///         or a channel twice; `ErrorCode::OutOfRange` for a policy channel the frame does not have;
///         `ErrorCode::IncompatibleArguments` for a solution of another frame size; any failure
///         `planStarDetection` returns for the request; or the first failure `detectStars` returns.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarDetectionChannelResult>
detectStarsByChannelPolicy(const ExecutionContext& execution, const StarFrameView& frame,
                           const StarDetectionRequest& request, const StarDetectionChannelPolicy& policy,
                           const WcsSolution* solution = nullptr);

} // namespace an
