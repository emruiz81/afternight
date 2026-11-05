// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent stock/private decoder executable. It never links either AfterNight engine.
#if defined(AN_PRIVATE_ORACLE)
#include <afternight-libraw/libraw/libraw.h>
#else
#include <libraw/libraw.h>
#endif
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <Windows.h>
#include <DbgHelp.h>
#endif

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace
{
#if defined(_WIN32)
LONG WINAPI crashTrace(EXCEPTION_POINTERS* exception)
{
    auto process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = exception->ContextRecord->Rip;
    frame.AddrStack.Offset = exception->ContextRecord->Rsp;
    frame.AddrFrame.Offset = exception->ContextRecord->Rbp;
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    for (unsigned i = 0; i < 24; ++i)
    {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 1024]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 1024;
        DWORD64 displacement = 0;
        SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol);
        std::cerr << std::hex << frame.AddrPC.Offset << ' ' << symbol->Name << '+' << displacement << '\n';
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, exception->ContextRecord,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
struct State
{
    bool corrupt = false;
    uint32_t cancelPhase = UINT32_MAX;
    uint64_t cancelAfter = UINT64_MAX;
    bool cancelled = false;
    Clock::time_point cancelledAt{};
};
void dataError(void* state, const char*, INT64) noexcept
{
    static_cast<State*>(state)->corrupt = true;
}
int stop(void* opaque, uint32_t phase, uint64_t completed) noexcept
{
    auto& state = *static_cast<State*>(opaque);
    if (!state.cancelled && phase == state.cancelPhase && completed >= state.cancelAfter)
    {
        state.cancelled = true;
        state.cancelledAt = Clock::now();
    }
    return state.cancelled ? 1 : 0;
}
std::filesystem::path path(const std::string& value)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
double milliseconds(Clock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
uint64_t hash(const std::vector<uint16_t>& values)
{
    uint64_t result = 14695981039346656037ull;
    for (const auto value : values)
        result = (result ^ value) * 1099511628211ull;
    return result;
}
void write(const std::string& prefix, const char* suffix, const std::vector<uint16_t>& values)
{
    if (prefix.empty())
        return;
    std::ofstream stream(path(prefix + suffix), std::ios::binary);
    stream.write(reinterpret_cast<const char*>(values.data()), std::streamsize(values.size() * 2));
    if (!stream)
        throw std::runtime_error("Cannot write oracle samples");
}
Json metadata(const LibRaw& raw)
{
    const auto& c = raw.imgdata.color;
    const auto& s = raw.imgdata.sizes;
    const auto& i = raw.imgdata.idata;
    const auto& o = raw.imgdata.other;
    Json xtrans = Json::array(), absolute = Json::array();
    for (unsigned y = 0; y < 6; ++y)
    {
        Json visibleRow = Json::array(), absoluteRow = Json::array();
        for (unsigned x = 0; x < 6; ++x)
        {
            visibleRow.push_back(static_cast<unsigned>(i.xtrans[y][x]));
            absoluteRow.push_back(static_cast<unsigned>(i.xtrans_abs[y][x]));
        }
        xtrans.push_back(visibleRow);
        absolute.push_back(absoluteRow);
    }
    const size_t blackCount = size_t(c.cblack[4]) * c.cblack[5];
    if (blackCount > 4096)
        throw std::runtime_error("Invalid black tile");
    return {{"black", c.black},
            {"white", c.maximum},
            {"channelBlack", std::vector<unsigned>(c.cblack, c.cblack + 4)},
            {"blackRows", c.cblack[4]},
            {"blackColumns", c.cblack[5]},
            {"blackTile", std::vector<unsigned>(c.cblack + 6, c.cblack + 6 + blackCount)},
            {"cameraMultipliers", c.cam_mul},
            {"daylightMultipliers", c.pre_mul},
            {"cameraToRgb", c.rgb_cam},
            {"cameraToXyz", c.cam_xyz},
            {"matrix", c.cmatrix},
            {"orientation", s.flip},
            {"width", s.width},
            {"height", s.height},
            {"rawWidth", s.raw_width},
            {"rawHeight", s.raw_height},
            {"leftMargin", s.left_margin},
            {"topMargin", s.top_margin},
            {"pixelAspect", s.pixel_aspect},
            {"filters", i.filters},
            {"xtrans", xtrans},
            {"xtransAbsolute", absolute},
            {"colors", i.colors},
            {"make", std::string(i.make)},
            {"model", std::string(i.model)},
            {"description", std::string(o.desc)},
            {"artist", std::string(o.artist)},
            {"iso", o.iso_speed},
            {"aperture", o.aperture},
            {"focalLength", o.focal_len},
            {"exposure", o.shutter},
            {"timestamp", o.timestamp},
            {"exifColorSpace", c.ExifColorSpace},
            {"asShotApplied", c.as_shot_wb_applied},
            {"rawCount", i.raw_count}};
}
Json run(const Json& request)
{
    State state;
    state.cancelPhase = request.value("cancelPhase", UINT32_MAX);
    state.cancelAfter = request.value("cancelAfter", UINT64_MAX);
    LibRaw raw;
    raw.set_dataerror_handler(dataError, &state);
    raw.imgdata.rawparams.use_rawspeed = 0;
    raw.imgdata.rawparams.shot_select = request.value("imageIndex", 0u);
    raw.imgdata.rawparams.max_raw_memory_mb = request.value("memoryMiB", 512u);
    auto& p = raw.imgdata.params;
    p.use_fuji_rotate = 0;
    p.user_flip = 0;
    p.gamm[0] = p.gamm[1] = 1;
    p.no_auto_bright = 1;
    p.output_bps = 16;
    p.output_color = 1;
    for (float& gain : p.user_mul)
        gain = 1;
    const auto started = Clock::now();
    const int opened = raw.open_file(path(request.at("path").get<std::string>()).c_str());
    Json result{{"openCode", opened}, {"openMs", milliseconds(started)}, {"libraw", LibRaw::version()}};
    if (opened != LIBRAW_SUCCESS || state.corrupt)
        return result;
    const auto& u = raw.get_internal_data_pointer()->unpacker_data;
    result["bits"] = u.tiff_bps;
    result["compression"] = u.tiff_compress;
    result["samples"] = u.tiff_samples;
    result["sampleFormat"] = u.tiff_sampleformat;
    result["make"] = raw.imgdata.idata.make;
    result["model"] = raw.imgdata.idata.model;
    result["rawWidth"] = raw.imgdata.sizes.raw_width;
    result["rawHeight"] = raw.imgdata.sizes.raw_height;
    result["filters"] = raw.imgdata.idata.filters;
#if defined(AN_PRIVATE_ORACLE)
    AnRawIdentification identified{};
    identified.size = sizeof(identified);
    identified.version = 1;
    if (anLibRawIdentify(&raw, &identified))
        throw std::runtime_error("Classifier failed");
    result["family"] = identified.family;
    result["supported"] = identified.supported;
    result["predictor"] = identified.predictor;
    result["workspaceEstimate"] = identified.workspaceEstimate;
    result["mode"] = identified.mode;
    result["curveEntries"] = identified.curveEntries;
    result["tileWidth"] = identified.tileWidth;
    result["tileHeight"] = identified.tileHeight;
    result["leftMargin"] = identified.leftMargin;
    result["topMargin"] = identified.topMargin;
    result["visibleWidth"] = identified.visibleWidth;
    result["visibleHeight"] = identified.visibleHeight;
    AnRawOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.family = identified.family;
    options.enabled = request.value("accelerated", false);
    options.stop = stop;
    options.state = &state;
    if (anLibRawConfigure(&raw, &options))
    {
        result["configureCode"] = -1;
        return result;
    }
#else
    if (request.value("accelerated", false))
        throw std::runtime_error("Stock oracle cannot accelerate");
#endif
    if (request.value("identifyOnly", false))
        return result;
    const auto unpackStarted = Clock::now();
    const int unpacked = raw.unpack();
    result["unpackCode"] = unpacked;
    result["unpackMs"] = milliseconds(unpackStarted);
    result["corrupt"] = state.corrupt;
    if (state.cancelled)
        result["cancellationLatencyMs"] = milliseconds(state.cancelledAt);
#if defined(AN_PRIVATE_ORACLE)
    AnRawResult actual{};
    actual.size = sizeof(actual);
    actual.version = 1;
    if (anLibRawResult(&raw, &actual))
        throw std::runtime_error("Result query failed");
    result["outcome"] = actual.outcome;
    result["correctionRevision"] = actual.correctionRevision;
    result["readMs"] = actual.readMicroseconds / 1000.;
    result["decoderMs"] = actual.unpackMicroseconds / 1000.;
    result["correctionMs"] = actual.correctionMicroseconds / 1000.;
#endif
    if (unpacked != LIBRAW_SUCCESS || state.corrupt)
        return result;
    result["metadata"] = metadata(raw);
    std::vector<uint16_t> sensor;
    const auto& sizes = raw.imgdata.sizes;
    if (raw.imgdata.rawdata.raw_image)
    {
        sensor.resize(size_t(sizes.raw_width) * sizes.raw_height);
        for (unsigned y = 0; y < sizes.raw_height; ++y)
            std::memcpy(sensor.data() + size_t(y) * sizes.raw_width,
                        raw.imgdata.rawdata.raw_image + size_t(y) * sizes.raw_pitch / 2, size_t(sizes.raw_width) * 2);
    }
    const bool fullColor =
        raw.imgdata.idata.colors != 1 && (raw.imgdata.idata.is_foveon || raw.imgdata.idata.filters <= 1);
    const auto count = size_t(sizes.width) * sizes.height;
    const auto width = sizes.width, height = sizes.height;
    std::vector<uint16_t> visible(count * (fullColor ? 3 : 1));
    if (fullColor)
    {
        if (raw.dcraw_process())
            throw std::runtime_error("Full-color processing failed");
        int code = 0;
        std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)> image(
            raw.dcraw_make_mem_image(&code), LibRaw::dcraw_clear_mem);
        if (code || !image || image->colors != 3 || image->bits != 16 || image->width != width ||
            image->height != height)
            throw std::runtime_error("Wrong full-color geometry");
        for (size_t i = 0; i < count; ++i)
            for (unsigned c = 0; c < 3; ++c)
                std::memcpy(&visible[c * count + i], image->data + (i * 3 + c) * 2, 2);
    }
    else
    {
        // Intentionally use the independent library extraction, not the engine's direct-copy implementation.
        if (raw.raw2image() || raw.subtract_black())
            throw std::runtime_error("Sensor extraction failed");
        if (sizes.iwidth != width || sizes.iheight != height)
            throw std::runtime_error("Wrong sensor geometry");
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
            {
                const auto index = size_t(y) * width + x;
                visible[index] = raw.imgdata.image[index][raw.imgdata.idata.colors == 1 ? 0 : raw.COLOR(y, x)];
            }
    }
    if (state.corrupt)
        throw std::runtime_error("Decoder data warning");
    result["sensorHash"] = hash(sensor);
    result["visibleHash"] = hash(visible);
    result["sensorSamples"] = sensor.size();
    result["visibleSamples"] = visible.size();
    const auto dump = request.value("dump", std::string{});
    write(dump, ".sensor", sensor);
    write(dump, ".visible", visible);
    return result;
}
} // namespace
int main()
{
#if defined(_WIN32)
    SetUnhandledExceptionFilter(crashTrace);
#endif
    std::string line;
    while (std::getline(std::cin, line))
    {
        try
        {
            std::cout << run(Json::parse(line)).dump() << std::endl;
        }
        catch (const std::exception& error)
        {
            std::cout << Json{{"error", error.what()}}.dump() << std::endl;
        }
        catch (...)
        {
            std::cout << "{\"error\":\"Unhandled decoder exception\"}" << std::endl;
        }
    }
}
