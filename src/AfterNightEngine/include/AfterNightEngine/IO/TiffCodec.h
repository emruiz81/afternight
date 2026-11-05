// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The TIFF adapter: libtiff behind the codec interface.
///
/// `createTiffCodec` builds an adapter to register in a `CodecRegistry`. It is reentrant: each call
/// owns its library state, and nothing is created or kept on the caller's behalf - no registry, engine
/// context or global default. It probes, decodes whole images and encodes; it has no region reader.
///
/// **What a read returns.** The representation, arrangement and row order the container declares:
/// every integer and floating-point width the format spells, contiguous samples as an interleaved
/// layout and separate planes as a planar one, sixty-four-bit integers and half-precision floats as
/// exact codes that are not resident sample types, and a bottom-up file as a bottom-up description
/// rather than a silently flipped image. The photometric interpretations whose samples are not what
/// they appear to be - a palette index, a white-is-zero grey, separated inks, subsampled chroma - are
/// refused by name rather than read as grey levels. Tags, Exif and
/// a mosaic's filter array travel as preserved cards and as the typed facts they state.
///
/// **What a write stores.** Every representation a read describes, in the arrangement of the frame it
/// is given, written only to the request's pending path; `writeImage` supplies atomic publication. The
/// compression and its level travel on the output intent, and a payload too large for a classic
/// container is written as a BigTIFF one. Unsupported representations and options are refused before
/// the pending file is opened.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <memory>

namespace an
{
class ImageCodec;

/// Creates the TIFF adapter.
///
/// @return the adapter, or `ErrorCode::AllocationFailed` when it cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::shared_ptr<const ImageCodec>> createTiffCodec();
} // namespace an
