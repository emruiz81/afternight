// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Private LibRaw extension. Maintained under the same dual licence as LibRaw.
#include "../../internal/libraw_cxx_defs.h"
#include "AnRawSpeed.h"

#include <chrono>
#include <cstring>
#include <limits>

int LibRaw::afternightIdentify(AnRawIdentification* result)
{
    if (!result || result->size != sizeof(*result) || result->version != 1)
        return LIBRAW_UNSPECIFIED_ERROR;
    *result = {};
    result->size = sizeof(*result);
    result->version = 1;
    const auto& unpacker = libraw_internal_data.unpacker_data;
    if (load_raw == &LibRaw::lossless_jpeg_load_raw && makeIs(LIBRAW_CAMERAMAKER_Canon))
        result->family = AnRawCanonCr2;
    else if (load_raw == &LibRaw::nikon_load_raw)
        result->family = AnRawNikonNef;
    else if (imgdata.idata.dng_version && load_raw == &LibRaw::lossless_dng_load_raw)
        result->family = AnRawBayerDng;
    else if (load_raw == &LibRaw::packed_load_raw && makeIs(LIBRAW_CAMERAMAKER_Fujifilm))
        result->family = AnRawFujiRaf;
    else if (load_raw == &LibRaw::sony_arw2_load_raw)
        result->family = AnRawSonyArw2;
    result->bits = unpacker.tiff_bps;
    result->compression = unpacker.tiff_compress;
    result->samples = unpacker.tiff_samples;
    result->sampleFormat = unpacker.tiff_sampleformat;
    for (const auto& ifd : tiff_ifd)
        if (ifd.offset == unpacker.data_offset && ifd.t_width == S.raw_width && ifd.t_height == S.raw_height)
        {
            result->predictor = ifd.predictor;
            result->curveEntries = ifd.lineartable_len;
        }
    result->tileWidth = unpacker.tile_width;
    result->tileHeight = unpacker.tile_length;
    result->leftMargin = S.left_margin;
    result->topMargin = S.top_margin;
    result->visibleWidth = S.width;
    result->visibleHeight = S.height;
    result->width = S.raw_width;
    result->height = S.raw_height;
    result->filters = P1.filters;
    result->rawCount = P1.raw_count;
    result->imageIndex = imgdata.rawparams.shot_select;
    const auto bytes = ID.input ? ID.input->size() : 0;
    result->encodedBytes = bytes > 0 ? uint64_t(bytes) : 0;
    // Inspect only compression headers. Preserve the original stream position on every exit.
    // No decoder is initialized, no file-sized copy is made, and no diagnostic text is parsed.
    if (ID.input && result->family)
    {
        struct Restore
        {
            LibRaw_abstract_datastream* input;
            INT64 position;
            ~Restore()
            {
                try
                {
                    input->seek(position, SEEK_SET);
                }
                catch (...)
                {
                }
            }
        } restore{ID.input, ID.input->tell()};
        if (result->family == AnRawNikonNef)
        {
            ID.input->seek(unpacker.meta_offset, SEEK_SET);
            const auto first = ID.input->get_char(), second = ID.input->get_char();
            result->mode = first < 0 || second < 0 ? UINT32_MAX : (uint32_t(first) << 8) | second;
        }
        else if (result->family == AnRawCanonCr2 || result->family == AnRawBayerDng)
        {
            ID.input->seek(unpacker.data_offset, SEEK_SET);
            if (result->family == AnRawBayerDng && result->tileHeight < INT_MAX)
                ID.input->seek(get4(), SEEK_SET);
            jhead header{};
            result->mode = ljpeg_start(&header, 1) && header.algo == 0xc3 && !header.sraw
                               ? (uint32_t(header.bits) << 16) | (uint32_t(header.clrs) << 8) | uint32_t(header.psv)
                               : UINT32_MAX;
        }
    }
    libraw_decoder_info_t info{};
    get_decoder_info(&info);
    result->supported = result->family && bytes > 0 && bytes < 2147483615LL && !IO.fuji_width && P1.raw_count == 1 &&
                        imgdata.rawparams.shot_select == 0 && P1.filters > 1 &&
                        (info.decoder_flags & LIBRAW_DECODER_TRYRAWSPEED3);
    // Aligned sensor rows, encoded copy, lookup tables and conservative database/parser allowance.
    // This is an overflow-safe preflight estimate, not a bound on the third-party peak.
    const uint64_t pitch = (uint64_t(S.raw_width) * 2 + 63) & ~uint64_t(63);
    result->workspaceEstimate = result->encodedBytes + 32 + pitch * S.raw_height + 131072 + 4 * 1024 * 1024;
    return LIBRAW_SUCCESS;
}

int LibRaw::afternightConfigure(const AnRawOptions* options) noexcept
{
    if (!options || options->size != sizeof(*options) || options->version != 1 || options->enabled > 1 ||
        (options->enabled && (options->family < AnRawCanonCr2 || options->family > AnRawSonyArw2)))
        return LIBRAW_UNSPECIFIED_ERROR;
    m_anOptions = *options;
    imgdata.rawparams.use_rawspeed = 0;
    m_anResult = {};
    m_anResult.size = sizeof(m_anResult);
    m_anResult.version = 1;
    m_anResult.correctionRevision = anRsCorrectionRevision();
    return LIBRAW_SUCCESS;
}

int LibRaw::afternightResult(AnRawResult* result) noexcept
{
    if (!result || result->size != sizeof(*result) || result->version != 1)
        return LIBRAW_UNSPECIFIED_ERROR;
    *result = m_anResult;
    return LIBRAW_SUCCESS;
}

int LibRaw::afternightUnpack()
{
    using Clock = std::chrono::steady_clock;
    AnRawIdentification identified{};
    identified.size = sizeof(identified);
    identified.version = 1;
    afternightIdentify(&identified);
    auto stopped = [&](uint32_t phase, uint64_t completed) {
        return m_anOptions.stop && m_anOptions.stop(m_anOptions.state, phase, completed);
    };
    auto failed = [&](uint32_t outcome, int code) {
        m_anResult.outcome = outcome;
        return code;
    };
    if (!identified.supported || identified.family != m_anOptions.family)
        return failed(AnRawUnsupported, LIBRAW_FILE_UNSUPPORTED);
    if (stopped(AnRsRead, 0))
        return failed(AnRawCancelled, LIBRAW_CANCELLED_BY_CALLBACK);
    m_anResult.workspaceBytes = identified.workspaceEstimate;
    if (identified.workspaceEstimate > uint64_t(imgdata.rawparams.max_raw_memory_mb) * 1024 * 1024)
        return failed(AnRawMemoryRefused, LIBRAW_TOO_BIG);
    AnRsOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.family = identified.family;
    options.correction = identified.family == AnRawNikonNef   ? AnRsNikonIntegerCurve
                         : identified.family == AnRawSonyArw2 ? AnRsSonyIntegerCurve
                                                              : AnRsUnmodified;
    options.width = identified.width;
    options.height = identified.height;
    options.filters = identified.filters;
    if (identified.filters == 9)
        for (unsigned y = 0; y < 6; ++y)
            for (unsigned x = 0; x < 6; ++x)
                options.xtrans[y * 6 + x] = P1.xtrans_abs[y][x];
    options.stop = m_anOptions.stop;
    options.state = m_anOptions.state;
    const auto readStarted = Clock::now();
    m_anEncoded = malloc(size_t(identified.encodedBytes) + 32);
    if (!m_anEncoded)
        return failed(AnRawAllocationFailed, LIBRAW_UNSUFFICIENT_MEMORY);
    ID.input->seek(0, SEEK_SET);
    for (uint64_t offset = 0; offset < identified.encodedBytes;)
    {
        if (stopped(AnRsRead, offset))
            return failed(AnRawCancelled, LIBRAW_CANCELLED_BY_CALLBACK);
        const size_t chunk = size_t((std::min)(uint64_t(1024 * 1024), identified.encodedBytes - offset));
        if (ID.input->read(static_cast<unsigned char*>(m_anEncoded) + offset, 1, chunk) != int(chunk))
            return failed(AnRawMalformed, LIBRAW_IO_ERROR);
        offset += chunk;
    }
    std::memset(static_cast<unsigned char*>(m_anEncoded) + identified.encodedBytes, 0, 32);
    m_anResult.readMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - readStarted).count();
    AnRsResult decoded{};
    decoded.size = sizeof(decoded);
    decoded.version = 1;
    int status = anRsCreate(&options, &_rawspeed3_handle);
    if (status == AnRsOk)
        status = anRsDecode(_rawspeed3_handle, m_anEncoded, size_t(identified.encodedBytes), &decoded);
    switch (status)
    {
    case AnRsOk:
        break;
    case AnRsCancelled:
        return failed(AnRawCancelled, LIBRAW_CANCELLED_BY_CALLBACK);
    case AnRsAllocationFailed:
        return failed(AnRawAllocationFailed, LIBRAW_UNSUFFICIENT_MEMORY);
    case AnRsUnsupported:
        return failed(AnRawUnsupported, LIBRAW_FILE_UNSUPPORTED);
    case AnRsWarnings:
        return failed(AnRawWarnings, LIBRAW_DATA_ERROR);
    case AnRsGeometryMismatch:
        return failed(AnRawGeometryMismatch, LIBRAW_DATA_ERROR);
    case AnRsInvalidArgument:
        return failed(AnRawInvalidArgument, LIBRAW_UNSPECIFIED_ERROR);
    case AnRsInternal:
        return failed(AnRawInternal, LIBRAW_UNSPECIFIED_ERROR);
    default:
        return failed(AnRawMalformed, LIBRAW_DATA_ERROR);
    }
    if (stopped(AnRsPublish, 0))
        return failed(AnRawCancelled, LIBRAW_CANCELLED_BY_CALLBACK);
    imgdata.rawdata.raw_image = const_cast<ushort*>(decoded.pixels);
    S.raw_pitch = decoded.pitch;
    imgdata.process_warnings |= LIBRAW_WARN_RAWSPEED3_PROCESSED;
    m_anResult.outcome = AnRawAccelerated;
    m_anResult.unpackMicroseconds = decoded.unpackMicroseconds;
    m_anResult.correctionMicroseconds = decoded.correctionMicroseconds;
    return LIBRAW_SUCCESS;
}

extern "C" {
int anLibRawIdentify(void* decoder, AnRawIdentification* result) noexcept
{
    try
    {
        return decoder ? static_cast<LibRaw*>(decoder)->afternightIdentify(result) : LIBRAW_UNSPECIFIED_ERROR;
    }
    catch (const std::bad_alloc&)
    {
        return LIBRAW_UNSUFFICIENT_MEMORY;
    }
    catch (...)
    {
        return LIBRAW_UNSPECIFIED_ERROR;
    }
}
int anLibRawConfigure(void* decoder, const AnRawOptions* options) noexcept
{
    try
    {
        return decoder ? static_cast<LibRaw*>(decoder)->afternightConfigure(options) : LIBRAW_UNSPECIFIED_ERROR;
    }
    catch (...)
    {
        return LIBRAW_UNSPECIFIED_ERROR;
    }
}
int anLibRawResult(void* decoder, AnRawResult* result) noexcept
{
    try
    {
        return decoder ? static_cast<LibRaw*>(decoder)->afternightResult(result) : LIBRAW_UNSPECIFIED_ERROR;
    }
    catch (...)
    {
        return LIBRAW_UNSPECIFIED_ERROR;
    }
}
}
