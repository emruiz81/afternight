// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The PNG adapter: libpng behind the codec interface.
///
/// `createPngCodec` builds an adapter to register in a `CodecRegistry`. It is reentrant: each call
/// owns its library state, and nothing is created or kept on the caller's behalf - no registry, engine
/// context or global default. It probes, decodes whole images and encodes; it has no region reader.
///
/// **What a read returns.** What the container declares, converted no further than decoding a PNG
/// requires: a palette becomes the colour samples it stands for, and a grey sample narrower than a byte
/// becomes eight bits at the same fraction of full scale. Sixteen-bit samples arrive in the host's
/// byte order, with the file's own order recorded as a source fact. The colour declaration the file
/// carries - an sRGB chunk, a gamma, an embedded profile - decides the description's colour basis and
/// numeric domain and the transfer function its metadata states; a file that declares none claims
/// none. Text chunks and Exif travel as preserved cards.
///
/// **What a write stores.** Prepared eight- or sixteen-bit samples in one to four channels, planar or
/// interleaved, written only to the request's pending path; `writeImage` supplies atomic publication.
/// The compression level travels on the output intent. A frame whose rows run bottom-up, or whose
/// samples are still a mosaic, is refused by name rather than written upside down or flattened, and
/// every unsupported representation or option is refused before the pending file is opened.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <memory>

namespace an
{
class ImageCodec;

/// Creates the PNG adapter.
///
/// @return the adapter, or `ErrorCode::AllocationFailed` when it cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::shared_ptr<const ImageCodec>> createPngCodec();
} // namespace an
