// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - whether an output intent states a form of the payload that only some containers hold.
//
// Byte shuffling, the size of the pieces a payload is compressed in on its own and a checksum of the stored bytes are
// steps of the XISF container. An adapter of a container that has none of them refuses an intent that states one, as
// it refuses a compression it does not write, so a statement is never written as something else or dropped.

#pragma once

#include <AfterNightEngine/Image/OutputIntent.h>

namespace an
{
/// Whether an intent states the form of its payload beyond its compression: byte shuffling, a compression block
/// size or a checksum.
/// @param intent the intent the encoder was handed, or null for none.
/// @return true when any of the three is stated.
[[nodiscard]] inline bool statesPayloadForm(const OutputIntent* intent) noexcept
{
    return intent != nullptr && (intent->byteShuffle().has_value() || intent->compressionBlockBytes().has_value() ||
                                 intent->checksum() != OutputChecksum::None);
}
} // namespace an
