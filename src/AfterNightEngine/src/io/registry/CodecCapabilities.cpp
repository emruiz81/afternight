// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the coherence check a codec adapter's capability record passes at
// registration.
//
// Twenty sample bits cover every raw class and width pair and three layout bits every storage
// arrangement; a bit outside those, an empty layout set, a zero channel ceiling or a region reader
// with no decoder makes the record incoherent.

#include <AfterNightEngine/IO/CodecCapabilities.h>

namespace an
{
bool validCodecCapabilities(const CodecCapabilities& capabilities) noexcept
{
    constexpr std::uint32_t kSamples = (1u << 20u) - 1u;
    return isCodecFormat(capabilities.format) && capabilities.format != CodecFormat::Unknown &&
           (capabilities.decodeSamples & ~kSamples) == 0u && (capabilities.encodeSamples & ~kSamples) == 0u &&
           capabilities.decodeLayouts != 0u && capabilities.decodeLayouts <= 7u && capabilities.encodeLayouts != 0u &&
           capabilities.encodeLayouts <= 7u && capabilities.maxChannels != 0u &&
           (!capabilities.regionDecode || capabilities.decodeSamples != 0u);
}
} // namespace an
