// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The sidecar the application writes beside a shared buffer it lends an extension, as neutral cases.
//
// A case is a header's cards over a small frame, written as a neutral FITS file, and the way the application lends
// it: the whole image, or a preview area of it. The record holds the reference descriptor and sidecar of each case;
// the process framework suite exports the same file through the application's own seam and compares the two after
// normalizing what differs by session - identifiers, paths and the file name's directory - holding every other
// difference to the register beside the record. The record is `sdk/shared-buffer-sidecars.json`.

#include "FixtureImage.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// The record of the reference descriptors and sidecars, under the expectation directory.
inline constexpr std::string_view kSidecarRecordFile = "sdk/shared-buffer-sidecars.json";

/// The register of every place the current seam's descriptor or sidecar differs from the record, under the
/// expectation directory.
inline constexpr std::string_view kSidecarDifferencesFile = "sdk/shared-buffer-sidecar-differences.json";

/// A preview area of an image, as the application lends one: its position and size in the whole image.
struct SidecarRegion
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
};

/// One lending of one image.
struct SidecarCase
{
    /// The record's call, `sidecar.<what>`.
    std::string call;
    /// The image, written as `<name>.fits` and read back through the seam.
    FixtureImage image;
    /// The preview area lent, or nothing for the whole image.
    std::optional<SidecarRegion> region;
};

/// Every case, in the record's order.
[[nodiscard]] const std::vector<SidecarCase>& sidecarCases();

} // namespace an::tests::fixtures
