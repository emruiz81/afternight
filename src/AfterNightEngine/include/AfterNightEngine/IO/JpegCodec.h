// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The JPEG adapter: libjpeg-turbo behind the codec interface.
///
/// `createJpegCodec` builds an adapter to register in a `CodecRegistry`. It is reentrant: each call
/// owns its library state, and nothing is created or kept on the caller's behalf - no registry, engine
/// context or global default. It probes, decodes whole images and encodes; it has no region reader.
///
/// **What a read returns.** The eight-bit grey or red-green-blue samples the container's own colour
/// conversion yields, interleaved for colour, in a description that says the magnitudes are
/// nonlinearly encoded and claims no colour basis, because a JPEG file declares none. The two
/// four-component separated sets this engine cannot describe, and every sample precision other than
/// eight bits, are refused by name rather than read as something else. Exif fields and comment markers
/// travel as preserved cards, the orientation a camera recorded included; it is reported, not applied,
/// because rotating the samples is a separate, explicit operation.
///
/// **What a write stores.** Prepared eight-bit grey or red-green-blue samples, planar or interleaved,
/// written only to the request's pending path; `writeImage` supplies atomic publication. The output
/// intent must state the JPEG compression, since a JPEG file has no other, and it carries the quality.
/// Unsupported representations and options are refused before the pending file is opened.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <memory>

namespace an
{
class ImageCodec;

/// Creates the JPEG adapter, preparing once the software card every file it writes carries.
///
/// @return the adapter; `ErrorCode::AllocationFailed` when it cannot be allocated; or the failure
///         preparing that card reported.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::shared_ptr<const ImageCodec>> createJpegCodec();
} // namespace an
