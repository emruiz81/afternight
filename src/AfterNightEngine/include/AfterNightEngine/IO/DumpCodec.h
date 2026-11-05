// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The developer dump format: an image's samples and full description, stored exactly.
///
/// A dump is a versioned 128-byte header followed by the samples. The header records the geometry, the
/// sample representation, a physical byte order for samples wider than a byte, the channel meaning,
/// the filter array with its phase and, for X-Trans, its table, the coordinate origin, the pixel aspect
/// and the layout; every field is written one by one in little-endian order, so neither the host's
/// byte order nor a C++ struct layout is part of the format. Samples are stored bit for bit in any of
/// the twenty raw representations, planar or interleaved as given, with row padding removed.
///
/// The format reserves no extension: a request selects it with `CodecFormat::Dump`. It carries no
/// metadata - no acquisition record, astrometric solution or lineage - and a decode returns empty
/// metadata.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <memory>

namespace an
{
class ImageCodec;

/// Creates the developer dump adapter, which probes, decodes whole images and regions, and encodes.
///
/// A read checks every header field, and the file's exact size, before it allocates the frame from the
/// engine context. A region allocates only its own samples, a full-width region is read in contiguous
/// strips, and every transfer is at most 4 MiB with a cancellation check before it. A probe that
/// validates the payload checks the file's exact size and still reports
/// `CodecProbeDisposition::DecodeRequired`, because a complete file is not proof that every sector
/// reads. The adapter is reentrant; each read owns its own file handle.
///
/// A read fails with `ErrorCode::OutOfRange` for an image index other than zero;
/// `ErrorCode::ExternalFailure` with `CodecFailure::MetadataUnreadable` when the file cannot be opened;
/// `ErrorCode::InvalidData` with `CodecFailure::MetadataUnreadable` for a file shorter than its header
/// or a header that is not a valid version-one header, or with `CodecFailure::PayloadUnreadable` for a
/// file whose size is not exactly the header and the samples or a read that comes up short; or
/// `ErrorCode::LimitExceeded` with `CodecFailure::MetadataUnreadable` for an extent beyond the engine
/// context's limits.
///
/// An encode writes only the request's pending path; publishing it atomically is `writeImage`'s job. It
/// fails with `ErrorCode::Unsupported` with `CodecFailure::UnsupportedOperation` for an output intent
/// that states compression, scaling or cards, or with `CodecFailure::InvalidMetadata` for processing
/// semantics other than the default, which the format cannot store; `ErrorCode::LimitExceeded` for a
/// packed layout beyond the engine context's limits; or `ErrorCode::ExternalFailure` with
/// `CodecFailure::DestinationUnavailable` when the pending path cannot be opened, or with
/// `CodecFailure::ShortWrite` when a write or the close fails.
///
/// @return the adapter, or `ErrorCode::AllocationFailed` when it cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::shared_ptr<const ImageCodec>> createDumpCodec();
} // namespace an
