// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Camera-database construction is derived from LibRaw 0.22.1's RawSpeed3 C wrapper.
// Copyright LibRaw LLC; see the complete upstream notices installed with this package.
// The versioned boundary, validation and cancellation policy are maintained by AfterNight.
#include "AnRawSpeed.h"
#include "RawSpeed-API.h"
#include "decoders/ArwDecoder.h"
#include "decoders/Cr2Decoder.h"
#include "decoders/DngDecoder.h"
#include "decoders/NefDecoder.h"
#include "decoders/RafDecoder.h"
#include <pugixml.hpp>

#include <chrono>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

extern const char* anRawSpeedCameraXml;

namespace
{
using Clock = std::chrono::steady_clock;

class CameraDatabase final : public rawspeed::CameraMetaData
{
public:
    CameraDatabase()
    {
        pugi::xml_document document;
        const auto loaded = document.load_string(anRawSpeedCameraXml);
        if (loaded.status == pugi::status_out_of_memory)
            throw std::bad_alloc();
        if (!loaded)
            throw std::runtime_error("Invalid embedded camera database");
        for (const auto node : document.child("Cameras").children("Camera"))
        {
            const auto* camera = addCamera(std::make_unique<rawspeed::Camera>(node));
            if (camera)
                for (size_t i = 0; i < camera->aliases.size(); ++i)
                    addCamera(std::make_unique<rawspeed::Camera>(camera, i));
        }
    }
};

struct Handle
{
    AnRsOptions options;
    CameraDatabase cameras;
    std::unique_ptr<rawspeed::Buffer> input;
    std::unique_ptr<rawspeed::RawParser> parser;
    std::unique_ptr<rawspeed::RawDecoder> decoder;
    uint32_t phase = AnRsInitialize;
    uint64_t checkpoints = 0;
    bool cancelled = false;

    explicit Handle(const AnRsOptions& value) : options(value) {}
    void enter(uint32_t value) noexcept
    {
        phase = value;
        checkpoints = 0;
    }
    bool stopped()
    {
        if (options.stop && options.stop(options.state, phase, ++checkpoints))
            cancelled = true;
        return cancelled;
    }
    static int checkpoint(void* state) noexcept
    {
        auto& handle = *static_cast<Handle*>(state);
        try
        {
            return handle.stopped() ? 1 : 0;
        }
        catch (...)
        {
            handle.cancelled = true;
            return 1;
        }
    }
    void reset() noexcept
    {
        decoder.reset();
        parser.reset();
        input.reset();
    }
};

int fail(Handle* handle, AnRsResult* result, int status) noexcept
{
    if (handle)
        handle->reset();
    *result = {};
    result->size = sizeof(*result);
    result->version = 1;
    result->status = status;
    return status;
}

bool matchesFamily(const rawspeed::RawDecoder& decoder, uint32_t family)
{
    switch (family)
    {
    case AnRsCanonCr2:
        return dynamic_cast<const rawspeed::Cr2Decoder*>(&decoder) != nullptr;
    case AnRsNikonNef:
        return dynamic_cast<const rawspeed::NefDecoder*>(&decoder) != nullptr;
    case AnRsBayerDng:
        return dynamic_cast<const rawspeed::DngDecoder*>(&decoder) != nullptr;
    case AnRsFujiRaf:
        return dynamic_cast<const rawspeed::RafDecoder*>(&decoder) != nullptr;
    case AnRsSonyArw2:
        return dynamic_cast<const rawspeed::ArwDecoder*>(&decoder) != nullptr;
    default:
        return false;
    }
}
bool matchesCfa(const rawspeed::ColorFilterArray& cfa, const AnRsOptions& options)
{
    const auto size = cfa.getSize();
    if (options.filters == 9)
    {
        if (size.x != 6 || size.y != 6)
            return false;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x)
                if (static_cast<unsigned>(cfa.getColorAt(x, y)) != options.xtrans[y * 6 + x])
                    return false;
        return true;
    }
    if (options.filters <= 1 || size.x != 2 || size.y != 2)
        return false;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 2; ++x)
        {
            const auto slot = (options.filters >> (y * 4 + x * 2)) & 3u;
            // LibRaw slot 3 is the second green; RawSpeed's CFA palette is RGB, not RGGB slots.
            // Calibration still uses LibRaw's original slots, never this comparison's palette.
            if (static_cast<unsigned>(cfa.getColorAt(x, y)) != (slot == 3u ? 1u : slot))
                return false;
        }
    return true;
}
uint64_t microseconds(Clock::time_point begin)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count();
}
} // namespace

extern "C" {
uint32_t anRsVersion() noexcept
{
    return 1;
}
uint32_t anRsCorrectionRevision() noexcept
{
    return 1;
}

int anRsCreate(const AnRsOptions* options, AnRsHandle* result) noexcept
{
    if (!result)
        return AnRsInvalidArgument;
    *result = nullptr;
    if (!options || options->size != sizeof(*options) || options->version != 1 || options->family < AnRsCanonCr2 ||
        options->family > AnRsSonyArw2 || options->width == 0 || options->height == 0 || options->width > 65535 ||
        options->height > 65535 || options->reserved != 0 ||
        options->correction != (options->family == AnRsNikonNef   ? AnRsNikonIntegerCurve
                                : options->family == AnRsSonyArw2 ? AnRsSonyIntegerCurve
                                                                  : AnRsUnmodified))
        return AnRsInvalidArgument;
    try
    {
        if (options->stop && options->stop(options->state, AnRsInitialize, 0))
            return AnRsCancelled;
        auto handle = std::make_unique<Handle>(*options);
        if (handle->stopped())
            return AnRsCancelled;
        *result = handle.release();
        return AnRsOk;
    }
    catch (const std::bad_alloc&)
    {
        return AnRsAllocationFailed;
    }
    catch (...)
    {
        return AnRsInternal;
    }
}

int anRsDecode(AnRsHandle opaque, const void* encoded, size_t bytes, AnRsResult* result) noexcept
{
    auto* handle = static_cast<Handle*>(opaque);
    if (!result || result->size != sizeof(*result))
    {
        if (handle)
            handle->reset();
        return AnRsInvalidArgument;
    }
    if (result->version != 1)
        return fail(handle, result, AnRsInvalidArgument);
    if (!handle || !encoded || bytes < 16 || bytes > 2147483615u)
        return fail(handle, result, AnRsInvalidArgument);
    fail(handle, result, AnRsInternal);
    try
    {
        handle->enter(AnRsUnpack);
        if (handle->stopped())
            return fail(handle, result, AnRsCancelled);
        const auto started = Clock::now();
        // RawParser and the decoder retain references to the Buffer object as well as its bytes.
        handle->input = std::make_unique<rawspeed::Buffer>(static_cast<const uint8_t*>(encoded), bytes);
        handle->parser = std::make_unique<rawspeed::RawParser>(*handle->input);
        handle->decoder = handle->parser->getDecoder(&handle->cameras);
        if (!handle->decoder || !matchesFamily(*handle->decoder, handle->options.family))
            return fail(handle, result, AnRsUnsupported);
        auto& decoder = *handle->decoder;
        decoder.applyCrop = false;
        decoder.failOnUnknown = true;
        decoder.interpolateBadPixels = false;
        decoder.applyStage1DngOpcodes = false;
        decoder.fujiRotate = false;
        decoder.uncorrectedRawValues = handle->options.correction != AnRsUnmodified;
        decoder.mRaw->anStop = Handle::checkpoint;
        decoder.mRaw->anStopState = handle;
        decoder.mRaw->anExpectedWidth = handle->options.width;
        decoder.mRaw->anExpectedHeight = handle->options.height;
        try
        {
            decoder.checkSupport(&handle->cameras);
        }
        catch (const std::bad_alloc&)
        {
            throw;
        }
        catch (...)
        {
            return fail(handle, result, AnRsUnsupported);
        }
        decoder.decodeMetaData(&handle->cameras);
        decoder.decodeRaw();
        const auto unpackTime = microseconds(started);
        if (handle->stopped())
            return fail(handle, result, AnRsCancelled);
        handle->enter(AnRsCorrect);
        const auto correctionStarted = Clock::now();
        // Both curve guards retain the integer table when uncorrectedRawValues is true.
        // Apply it exactly once, after unpack, without random rounding.
        if (handle->options.correction != AnRsUnmodified)
            decoder.mRaw->sixteenBitLookup();
        if (handle->stopped())
            return fail(handle, result, AnRsCancelled);
        const auto correctionTime = microseconds(correctionStarted);
        decoder.decodeMetaData(&handle->cameras);
        const auto& raw = decoder.mRaw;
        if (!raw->getErrors().empty())
            return fail(handle, result, AnRsWarnings);
        const auto dimensions = raw->getUncroppedDim();
        const auto filters = raw->cfa.getDcrawFilter();
        if (dimensions.x != handle->options.width || dimensions.y != handle->options.height || raw->getCpp() != 1 ||
            raw->getBpp() != 2 || raw->getDataType() != rawspeed::TYPE_USHORT16 ||
            raw->pitch < uint64_t(handle->options.width) * 2 || raw->pitch % 2 != 0 ||
            !matchesCfa(raw->cfa, handle->options) || uint64_t(raw->pitch) * dimensions.y > SIZE_MAX)
            return fail(handle, result, AnRsGeometryMismatch);
        handle->enter(AnRsPublish);
        if (handle->stopped())
            return fail(handle, result, AnRsCancelled);
        result->size = sizeof(*result);
        result->version = 1;
        result->status = AnRsOk;
        result->width = dimensions.x;
        result->height = dimensions.y;
        result->pitch = raw->pitch;
        result->filters = filters;
        result->components = 1;
        result->pixels = reinterpret_cast<const uint16_t*>(raw->getDataUncropped(0, 0));
        result->workspaceBytes = uint64_t(raw->pitch) * dimensions.y;
        result->unpackMicroseconds = unpackTime;
        result->correctionMicroseconds = correctionTime;
        return AnRsOk;
    }
    catch (const rawspeed::AnGeometryMismatch&)
    {
        return fail(handle, result, AnRsGeometryMismatch);
    }
    catch (const std::bad_alloc&)
    {
        return fail(handle, result, AnRsAllocationFailed);
    }
    catch (...)
    {
        return fail(handle, result, handle->cancelled ? AnRsCancelled : AnRsMalformed);
    }
}

void anRsDestroy(AnRsHandle handle) noexcept
{
    try
    {
        delete static_cast<Handle*>(handle);
    }
    catch (...)
    {
    }
}
}
