// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which decompressor a camera RAW read uses.
///
/// Kept apart from `IO/RawCodec.h` so that `CodecReadRequest` can carry a per-read override without
/// including the RAW adapter. Every choice returns the same camera codes and metadata; only the
/// decoder that produced them, and its speed, differ.

#pragma once

#include <cstdint>

namespace an
{
/// Which decompressor a camera RAW read uses; every choice keeps the same sample and metadata contract.
enum class RawDecoderPreference : std::uint8_t
{
    /// The accelerator for a decoder mode it is qualified for and routed to on this host, LibRaw
    /// otherwise; a recoverable accelerator failure is retried once through LibRaw on a freshly
    /// opened file.
    Automatic,
    /// LibRaw alone, skipping the accelerator's classification, its copy of the file and its camera
    /// database.
    NativeLibRaw,
    /// The accelerator or nothing: an unavailable or unqualified accelerator is
    /// `ErrorCode::UnsupportedBackend`, and a failure is never retried through LibRaw.
    RequireRawSpeed
};
} // namespace an
