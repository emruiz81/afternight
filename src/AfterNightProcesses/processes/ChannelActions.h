// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image menu's channel and conversion actions as one table, and what each action, and the in-place operations
/// beside them, does to one image.
///
/// Thirty-nine registered actions extract a channel or a colour component of an image or convert it to another
/// representation: thirty-two extractions - a stored channel, the three stored channels, one component or all three of
/// CIE L*a*b*, L*C*h, XYZ, HSL, HSV, HSI and YCbCr, and the a*b* view - and seven conversions. Every action is one row
/// of `channelActions()`, which a still image and the frames of a sequence share: the planes it makes, what each is
/// called, what it converts to; `runChannelAction` runs a row over one image, and the non-interactive processes run it
/// over the image a view shows and over every frame of the sequence it plays.
///
/// **An extraction is the engine's channel scope.** A stored channel is copied in the image's own representation and
/// keeps the image's meaning, cards and solution: it is the image's own samples. A colour component is the scope's
/// plane - Float32, Float64 for a Float64 image, every component of an in-gamut colour in the unit interval, a hue as a
/// fraction of a turn - converted from the image's red, green and blue read as the process framework reads them:
/// linear when the image says it is, sRGB-encoded otherwise, and an integer image as fractions of full scale, unsigned
/// of its largest code and signed counted from its smallest. A component is a measurement of the image, not the image:
/// it keeps the acquisition record, the solution and the lineage, and none of the cards. The a*b* view is a colour
/// image of a* as red, b* as green and a cleared blue.
///
/// **A conversion is a normalized mapping.** A sample is read as a fraction of its representation's range - an
/// unsigned integer of its largest value, a signed one counted from its smallest, a floating sample as it is - and
/// written as the same fraction of the target's, in double precision, to the nearest code and saturated at the target's
/// range; a floating target holds the fraction itself. The image keeps everything it states, and a value rule of its
/// validity that the mapping moves is carried as a mask.
///
/// **The in-place operations** - grey, colour, the negative and the turns - are here too, because a sequence frame
/// takes the same computation a still image does: the grey image is `0.2989 R + 0.5870 G + 0.1140 B` in the image's
/// representation, the colour image the grey plane in all three channels, the negative the complement of every sample
/// in fractions of full scale, weighed by a mask when one is given, and a turn or mirror the view's own.
///
/// Everything here runs on a worker, under the run's admitted execution, and makes new assets of its session; the image
/// read is never written.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace process_utils
{

/// What a channel action makes.
enum class ChannelActionKind : std::uint8_t
{
    /// One grey plane per output: a stored channel or a colour component.
    Extract,

    /// One colour image of a*, b* and a cleared blue: the a*b* view.
    ChromaView,

    /// The image in another representation.
    Convert
};

/// One plane an extraction makes.
struct ChannelActionOutput
{
    /// The plane.
    an::ChannelScope scope = an::ChannelScope::Red;

    /// What the view it is opened in is called after the source's name: " - RED", " - CIE L*".
    const char* suffix = "";

    /// What it is called in its history and the log: "Red channel", "CIE L*".
    const char* label = "";
};

/// One registered channel or conversion action.
struct ChannelAction
{
    /// The registered action: "actionExtract_Red_Channel".
    const char* action = "";

    /// The channel extractor's member, and the non-interactive process, it reaches: "extractRedChannel".
    const char* member = "";

    /// What it makes.
    ChannelActionKind kind = ChannelActionKind::Extract;

    /// The planes an extraction makes, in the order its views are opened; the first two of the a*b* view; none for a
    /// conversion.
    std::array<ChannelActionOutput, 3u> outputs{};

    /// How many of `outputs` it makes.
    std::uint32_t outputCount = 0u;

    /// What the a*b* view and a conversion's view are called after the source's name: " - CIE a*=R / b*=G",
    /// " - CONV_8_UINT".
    const char* suffix = "";

    /// What the a*b* view and a conversion are called in their history and the log: "CIE a*=R / b*=G", "8 bits".
    const char* label = "";

    /// The representation a conversion makes.
    an::SampleType target = an::SampleType::Float32;

    /// What its progress says over a sequence, before and after the sequence's name: "Extracting red channel from
    /// sequence ", "" - or "Converting sequence ", " to 8 bits".
    const char* progressBefore = "";
    const char* progressAfter = "";
};

/// The thirty-nine registered channel and conversion actions, in the order the registry registers them.
///
/// @return the table, static.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::span<const ChannelAction> channelActions() noexcept;

/// The row of a registered action.
///
/// @param action the action's object name.
/// @return the row, or null for an action the table does not hold.
[[nodiscard]] AFTERNIGHTPROCESSES_API const ChannelAction* channelActionNamed(std::string_view action) noexcept;

/// The row the channel extractor's member of a name runs.
///
/// @param member the member's name.
/// @return the row, or null for a member the table does not hold.
[[nodiscard]] AFTERNIGHTPROCESSES_API const ChannelAction* channelActionOfMember(std::string_view member) noexcept;

/// Whether an action reads a colour image: every extraction does, a conversion does not.
///
/// @param action the row.
/// @return true for an extraction and the a*b* view.
[[nodiscard]] constexpr bool channelActionNeedsColour(const ChannelAction& action) noexcept
{
    return action.kind != ChannelActionKind::Convert;
}

/// How many images an action makes of one image.
///
/// @param action the row.
/// @return its outputs for an extraction, and one otherwise.
[[nodiscard]] constexpr std::uint32_t channelActionImageCount(const ChannelAction& action) noexcept
{
    return action.kind == ChannelActionKind::Extract ? action.outputCount : 1u;
}

/// Runs an action over one image.
///
/// @param scope the run's admitted execution, whose session the images belong to.
/// @param source the image, read for the call.
/// @param action the row.
/// @return the images it makes, in the order the row names them - a conversion to the image's own representation an
///         exact copy; `ErrorCode::IncompatibleArguments` for an extraction of an image that is not a colour image; or
///         what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::vector<an::consumer::ImageAsset>>
runChannelAction(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source, const ChannelAction& action);

/// A colour image as one grey channel, `0.2989 R + 0.5870 G + 0.1140 B`, in its own representation, rounded to the
/// nearest code. It keeps everything the image states; its validity is the image's, a rule of each channel's values
/// carried as one mask that calls a pixel valid only where all three channels are.
///
/// @param scope the run's admitted execution.
/// @param source a colour image.
/// @return the grey image; `ErrorCode::IncompatibleArguments` for an image that is not a colour image; or what the
///         engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
greyImageOf(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source);

/// A grey image as three identical colour channels in its own representation. It keeps everything the image states,
/// and an explicit mask is shared by the three channels.
///
/// @param scope the run's admitted execution.
/// @param source a grey image.
/// @return the colour image; `ErrorCode::IncompatibleArguments` for an image that is not one grey channel; or what the
///         engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
colourImageOf(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source);

/// The negative of an image: every sample's complement in fractions of full scale - one minus a floating sample,
/// the largest code minus an unsigned one, and the sum of the smallest and the largest code minus a signed one -
/// weighed by a mask when one is given, as a process's result is. Recorded nonlinear on the processing-state cards
/// under the name "negative-transform". A sample the image's validity rejects is left as it was.
///
/// @param scope the run's admitted execution.
/// @param source the image.
/// @param blend the mask that weighs the result, or none.
/// @return the negative; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
negativeImageOf(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                const an::ui::ProcessBlendRequest& blend);

/// An image turned or mirrored as the view turns and mirrors its own: every sample moved, the description restated,
/// the solution invalidated, the cards dropped, and an explicit mask moved with the samples.
///
/// @param scope the run's admitted execution.
/// @param source the image.
/// @param orientation the turn or mirror.
/// @return the turned image; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
orientedImageOf(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                an::ImageOrientation orientation);

} // namespace process_utils
