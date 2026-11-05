// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the default adapter operations, each refusing with
// `ErrorCode::Unsupported`, so an adapter implements only what it can do.

#include <AfterNightEngine/IO/ImageCodec.h>

namespace an
{
Result<CodecDecodeResult> ImageCodec::decode(const EngineContext&, const ExecutionContext&,
                                             const CodecReadRequest&) const
{
    return Status::failure(ErrorCode::Unsupported);
}

Result<CodecDecodeResult> ImageCodec::decodeRegion(const EngineContext&, const ExecutionContext&,
                                                   const CodecReadRequest&, const CodecRegion&) const
{
    return Status::failure(ErrorCode::Unsupported);
}

Result<CodecEncodeReport> ImageCodec::encode(const EngineContext&, const ExecutionContext&, const CodecEncodeRequest&,
                                             const RawFrame&, const ImageMetadata&) const
{
    return Status::failure(ErrorCode::Unsupported);
}
} // namespace an
