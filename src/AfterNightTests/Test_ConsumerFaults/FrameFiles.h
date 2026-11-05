// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The consumer fault scenarios' input frames, written through the neutral fixture layer.
///
/// A unit of its own, reaching no engine header, because the neutral description of a test image and an engine type
/// meet only in a declared bridge.

#include <cstdint>
#include <filesystem>

namespace an::tests::consumerfaults
{

/// Writes a grey Float32 FITS frame of a gentle gradient with noise, through the neutral fixture layer.
///
/// @param path where.
/// @param width its width.
/// @param height its height.
/// @param seed the noise's seed, so frames of one group differ.
/// @param base its level, a fraction of full scale.
/// @return whether it was written; a failure is reported to the test as well.
[[nodiscard]] bool writeFrame(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                              std::uint64_t seed, double base = 0.3);

} // namespace an::tests::consumerfaults
