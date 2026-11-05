// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the structural validation of a sequence descriptor.
//
// Only what a descriptor says about itself is checked here: a known format, rates with no zero term, a
// duration that is not negative, and a time base wherever times are stated. What an engine context's
// limits admit is checked by the handles that adopt the descriptor.

#include <AfterNightEngine/IO/SequenceDescriptor.h>

namespace an
{
Status validateSequenceDescriptor(const SequenceDescriptor& descriptor) noexcept
{
    const auto validRate = [](const std::optional<SequenceRate>& rate) {
        return !rate || (rate->numerator != 0u && rate->denominator != 0u);
    };
    if (static_cast<std::uint8_t>(descriptor.format) > static_cast<std::uint8_t>(SequenceFormat::Video) ||
        !validRate(descriptor.timeBase) || !validRate(descriptor.nominalFrameRate) ||
        ((descriptor.seekByTime || descriptor.timestampsRequired || descriptor.durationTicks) &&
         !descriptor.timeBase) ||
        (descriptor.durationTicks && *descriptor.durationTicks < 0))
        return sequenceFailure(ErrorCode::InvalidArgument, SequenceFailure::InvalidDescriptor);
    return Status::success();
}
} // namespace an
