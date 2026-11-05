// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// Reading what the command-line tool wrote through the tool's own codecs, for a container the neutral fixture layer
// has no reader for. It proves what representation and shape a file holds, not what its samples mean; a FITS output
// is read by the neutral reader in `CliTestSupport.h` instead.

#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace an::cli::tests
{

/// What a file the tool wrote states, read through the tool's own codecs.
struct WrittenImage
{
    an::SampleType sampleType = an::SampleType::Float32;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;
};

/// Probes a written file; empty, with a failure added to the case, when it cannot be read.
[[nodiscard]] std::optional<WrittenImage> probeWritten(const std::filesystem::path& path);

} // namespace an::cli::tests
