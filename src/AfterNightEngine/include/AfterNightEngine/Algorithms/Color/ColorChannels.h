// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Shared scalar triples for red, green and blue operations.
///
/// The per-channel vocabulary the colour calibrations and the narrowband colour map report and
/// accept, kept in a header of its own that includes nothing but `<array>`, so a caller that only
/// names a gain or an offset does not inherit an algorithm's include closure. The aliases are plain
/// arrays: index zero is red, one green and two blue, with no alpha and no normalization.

#pragma once

#include <array>

namespace an
{

/// Three scalar values ordered red, green and blue.
///
/// The alias is a vocabulary type for results that measure a value per RGB channel. It owns its
/// three values, has no hidden normalization, and is safe to copy between threads.
using ColorChannelValues = std::array<double, 3u>;

/// Three per-channel multipliers ordered red, green and blue.
using ColorChannelGains = ColorChannelValues;

/// Three per-channel addends ordered red, green and blue, in the image's declared numeric domain.
using ColorChannelOffsets = ColorChannelValues;

} // namespace an
