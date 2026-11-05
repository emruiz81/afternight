// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// End-to-end RAW operation driver over the engine; it shares no oracle.
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/Image/DescriptorText.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TraceSink.h>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <future>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <Windows.h>
#include <Psapi.h>
#else
#include <sys/resource.h>
#endif

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace
{
double milliseconds(Clock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
uint64_t hash(const void* data, size_t bytes)
{
    uint64_t result = 14695981039346656037ull;
    const auto* input = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i + 1 < bytes; i += 2)
    {
        uint16_t value;
        std::memcpy(&value, input + i, 2);
        result = (result ^ value) * 1099511628211ull;
    }
    return result;
}
uint64_t peakWorkingSet()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS memory{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)) ? memory.PeakWorkingSetSize : 0;
#else
    rusage memory{};
    return getrusage(RUSAGE_SELF, &memory) == 0 ? uint64_t(memory.ru_maxrss) * 1024 : 0;
#endif
}

class Recorder final : public an::TraceSink
{
public:
    struct Event
    {
        std::string name;
        double value;
        uint64_t sequence;
        uint64_t elapsed;
    };
    std::vector<Event> events;
    std::stop_source source;
    std::string cancelEvent;
    std::string deadlineEvent;
    double cancelAfter = 128;
    Clock::time_point cancelledAt{};
    Clock::time_point deadline{};
    bool deadlineInjected = false;
    bool failed = false;
    bool* disableRawSpeedDuringRead = nullptr;
    Recorder()
    {
        events.reserve(256);
    }
    void onTrace(const an::TraceEvent& event) noexcept override
    {
        try
        {
            events.push_back({event.name, event.value, event.sequence, event.elapsedNanoseconds});
            if (disableRawSpeedDuringRead && std::string_view(event.name) == "rawspeed.unpack.checkpoint" &&
                event.value >= 128.)
                *disableRawSpeedDuringRead = false;
            if (!deadlineInjected && !deadlineEvent.empty() && deadlineEvent == event.name &&
                event.value >= cancelAfter)
            {
                deadlineInjected = true;
                // Exercise expiry inside the decoder without relying on a slow-enough fixture.
                std::this_thread::sleep_until(deadline + std::chrono::milliseconds(1));
            }
            if (!source.stop_requested() && cancelEvent == event.name && event.value >= cancelAfter)
            {
                cancelledAt = Clock::now();
                source.request_stop();
            }
        }
        catch (...)
        {
            failed = true;
            source.request_stop();
        }
    }
};

Json calibration(const an::CodecDecodeResult& decoded)
{
    const auto& acquisition = decoded.metadata.acquisition();
    Json result{{"instrument", acquisition.instrumentName()},
                {"artist", acquisition.observerName()},
                {"description", acquisition.objectName()}};
    if (acquisition.exposureSeconds())
        result["exposure"] = *acquisition.exposureSeconds();
    if (acquisition.exposureStart())
        result["timestampNanoseconds"] = acquisition.exposureStart()->nanosecondsSinceEpoch;
    if (!acquisition.rawCamera())
        return result;
    const auto& camera = *acquisition.rawCamera();
    result["camera"] = {{"black", camera.blackLevel},
                        {"white", camera.whiteLevel},
                        {"channelBlack", camera.channelBlack},
                        {"blackRows", camera.blackPatternRows},
                        {"blackColumns", camera.blackPatternColumns},
                        {"blackTile", camera.blackPattern},
                        {"cameraMultipliers", camera.cameraMultipliers},
                        {"daylightMultipliers", camera.daylightMultipliers},
                        {"cameraToRgb", camera.cameraToRgb},
                        {"cameraToXyz", camera.cameraToXyz},
                        {"iso", camera.isoSpeed},
                        {"aperture", camera.aperture},
                        {"focalLength", camera.focalLengthMillimeters},
                        {"orientation", camera.orientation},
                        {"exifColorSpace", camera.exifColorSpace},
                        {"asShotApplied", camera.asShotWhiteBalanceApplied}};
    return result;
}

an::RawDecoderPreference decoderPreference(const std::string& value)
{
    if (value == "automatic")
        return an::RawDecoderPreference::Automatic;
    if (value == "native")
        return an::RawDecoderPreference::NativeLibRaw;
    if (value == "required")
        return an::RawDecoderPreference::RequireRawSpeed;
    throw std::runtime_error("Invalid RAW decoder preference");
}

Json run(const Json& request)
{
    const auto path = request.at("path").get<std::string>();
    Json result;
    if (!an::engineMatchesHeaders())
        throw std::runtime_error("Engine/header ABI mismatch");
    an::RawCodecOptions rawOptions;
    rawOptions.decoderPreference = decoderPreference(request.value("adapterPreference", "automatic"));
    rawOptions.libraryMemoryLimitMegabytes = request.value("memoryMiB", 512u);
    rawOptions.fullColorWhiteBalance = static_cast<an::RawFullColorWhiteBalance>(request.value("whiteBalance", 0u));
    if (request.contains("multipliers"))
        for (unsigned c = 0; c < 3; ++c)
            rawOptions.fullColorMultipliers[c] = request["multipliers"][c];
    auto codec = an::createRawCodec(rawOptions);
    if (!codec.hasValue())
        throw std::runtime_error("Cannot create RAW codec");
    const std::array codecs{*codec};
    auto registry = an::CodecRegistry::create(codecs);
    if (!registry.hasValue())
        throw std::runtime_error("Cannot create registry");
    an::EngineContextOptions options;
    options.codecRegistry = *registry;
    options.maxConcurrency = request.value("threads", 1u);
    if (request.contains("engineMemoryBytes"))
        options.limits.memoryBudgetBytes = request["engineMemoryBytes"];
    auto engine = an::EngineContext::create(options);
    if (!engine.hasValue())
        throw std::runtime_error("Cannot create engine");
    if (request.value("testRuntimeToggle", false))
    {
        // One application-owned flag, one immutable adapter/context, and retained earlier frames.
        // Change the flag inside the first unpack: only the following read acquires the change.
        bool enableRawSpeed = true;
        std::vector<an::CodecDecodeResult> retained;
        result["reads"] = Json::array();
        for (unsigned i = 0; i < 3; ++i)
        {
            if (i == 2)
                enableRawSpeed = true;
            const auto preference =
                enableRawSpeed ? an::RawDecoderPreference::Automatic : an::RawDecoderPreference::NativeLibRaw;
            Recorder trace;
            if (i == 0)
                trace.disableRawSpeedDuringRead = &enableRawSpeed;
            auto operation = an::ExecutionContext::create(*engine, {.traceSink = &trace});
            if (!operation.hasValue())
                throw std::runtime_error("Cannot create toggle execution");
            auto frame = an::decodeImage(*engine, *operation, {.path = path, .rawDecoderPreference = preference});
            Json read{{"ok", frame.hasValue()},
                      {"status", an::errorCodeName(frame.status().code())},
                      {"enabledAfterRead", enableRawSpeed},
                      {"selected", "none"}};
            for (const auto& event : trace.events)
            {
                if (event.name == "selected.rawspeed")
                    read["selected"] = "rawspeed";
                if (event.name == "selected.native")
                    read["selected"] = "native";
            }
            if (trace.failed)
                throw std::runtime_error("Toggle trace recorder failed");
            if (frame.hasValue())
            {
                read["visibleHash"] = hash(frame->frame.bytes().data(), frame->frame.bytes().size());
                read["metadata"] = calibration(*frame);
                retained.push_back(std::move(*frame));
            }
            result["reads"].push_back(std::move(read));
            // A read Automatic keeps native never reaches the switch inside the unpack; turn it off
            // here too, so the second read is native on every host and for every mode.
            if (i == 0)
                enableRawSpeed = false;
        }
        result["retainedBytes"] = engine->memoryResource()->counters().liveBytes;
        retained.clear();
        result["engineLiveBytes"] = engine->memoryResource()->counters().liveBytes;
        return result;
    }
    Recorder recorder;
    recorder.cancelEvent = request.value("cancelEvent", "");
    recorder.deadlineEvent = request.value("deadlineEvent", "");
    recorder.cancelAfter = request.value("cancelAfter", 128.);
    an::ExecutionOptions executionOptions;
    executionOptions.traceSink = &recorder;
    executionOptions.stopToken = recorder.source.get_token();
    if (request.contains("deadlineMs"))
    {
        recorder.deadline = Clock::now() + std::chrono::milliseconds(request["deadlineMs"].get<int>());
        executionOptions.deadline = recorder.deadline;
    }
    auto execution = an::ExecutionContext::create(*engine, executionOptions);
    if (!execution.hasValue())
        throw std::runtime_error("Cannot create execution");
    std::optional<an::RawDecoderPreference> readPreference;
    if (request.contains("preference") && request.contains("enableRawSpeed"))
        throw std::runtime_error("Choose either preference or enableRawSpeed");
    if (request.contains("preference"))
        readPreference = decoderPreference(request["preference"]);
    if (request.contains("enableRawSpeed"))
        readPreference = request["enableRawSpeed"].get<bool>() ? an::RawDecoderPreference::Automatic
                                                               : an::RawDecoderPreference::NativeLibRaw;
    const auto started = Clock::now();
    auto decoded = an::decodeImage(*engine, *execution,
                                   {.path = path,
                                    .format = an::CodecFormat::Raw,
                                    .imageIndex = request.value("imageIndex", 0u),
                                    .readMetadata = request.value("metadata", true),
                                    .rawDecoderPreference = readPreference});
    result["operationMs"] = milliseconds(started);
    result["admittedThreads"] = execution->admittedConcurrency();
    result["processPeakWorkingSet"] = peakWorkingSet();
    result["enginePeakBytes"] = engine->memoryResource()->counters().peakBytes;
    result["engineLiveBytes"] = engine->memoryResource()->counters().liveBytes;
    result["ok"] = decoded.hasValue();
    result["status"] = an::errorCodeName(decoded.status().code());
    result["selected"] = "none";
    result["traces"] = Json::array();
    for (const auto& event : recorder.events)
    {
        result["traces"].push_back(
            {{"name", event.name}, {"value", event.value}, {"sequence", event.sequence}, {"elapsedNs", event.elapsed}});
        if (event.name == "selected.rawspeed")
            result["selected"] = "rawspeed";
        if (event.name == "selected.native")
            result["selected"] = "native";
    }
    if (recorder.failed)
        throw std::runtime_error("Trace recorder allocation failed");
    if (recorder.source.stop_requested())
        result["cancellationLatencyMs"] = milliseconds(recorder.cancelledAt);
    if (recorder.deadlineInjected)
    {
        result["deadlineInjected"] = true;
        result["deadlineLatencyMs"] = milliseconds(recorder.deadline);
    }
    if (decoded.hasValue())
    {
        const auto& frame = decoded->frame;
        const auto descriptor = *frame.descriptor();
        result["width"] = descriptor.extent().width();
        result["height"] = descriptor.extent().height();
        result["planes"] = frame.layout()->planeCount();
        result["visibleHash"] = hash(frame.bytes().data(), frame.bytes().size());
        result["visibleSamples"] = frame.bytes().size() / 2;
        result["adoptable"] = frame.checkAdoptable().ok();
        result["metadata"] = calibration(*decoded);
        const auto resident = descriptor.toImageDescriptor();
        if (!resident)
            throw std::runtime_error("Returned RAW descriptor is not resident");
        result["descriptorText"] = an::formatImageDescriptor(*resident);
        if (descriptor.isMosaiced())
        {
            result["cfaColors"] = Json::array();
            for (unsigned y = 0; y < 6; ++y)
            {
                Json row = Json::array();
                for (unsigned x = 0; x < 6; ++x)
                    row.push_back(static_cast<unsigned>(descriptor.cfa().colorAt(x, y)));
                result["cfaColors"].push_back(row);
            }
        }
        result["descriptor"] = {{"domain", static_cast<unsigned>(descriptor.numericDomain())},
                                {"colorSpace", static_cast<unsigned>(descriptor.colorSpace())},
                                {"channels", static_cast<unsigned>(descriptor.channelSchema())},
                                {"cfa", static_cast<unsigned>(descriptor.cfa().pattern())},
                                {"pixelAspect", descriptor.pixelAspectRatio()},
                                {"sampleBytes", descriptor.sampleFormat().sizeBytes},
                                {"sampleClass", static_cast<unsigned>(descriptor.sampleFormat().sampleClass)},
                                {"byteOrder", static_cast<unsigned>(descriptor.byteOrder())},
                                {"origin", static_cast<unsigned>(descriptor.coordinateOrigin())}};
        const auto dump = request.value("dump", std::string{});
        if (!dump.empty())
        {
            std::ofstream stream(
                std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(dump.data()), dump.size())),
                std::ios::binary);
            stream.write(reinterpret_cast<const char*>(frame.bytes().data()), std::streamsize(frame.bytes().size()));
            if (!stream)
                throw std::runtime_error("Cannot dump returned frame");
        }
    }
    return result;
}
} // namespace

int main()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        try
        {
            const auto request = Json::parse(line);
            if (request.contains("batch"))
            {
                const auto start = Clock::now();
                std::vector<std::future<Json>> workers;
                for (const auto& item : request["batch"])
                    workers.push_back(std::async(std::launch::async, [item] { return run(item); }));
                Json results = Json::array();
                for (auto& worker : workers)
                    results.push_back(worker.get());
                std::cout << Json{{"results", results}, {"wallMs", milliseconds(start)}}.dump() << std::endl;
            }
            else
                std::cout << run(request).dump() << std::endl;
        }
        catch (const std::exception& error)
        {
            std::cout << Json{{"error", error.what()}}.dump() << std::endl;
        }
        catch (...)
        {
            std::cout << "{\"error\":\"Unhandled engine exception\"}" << std::endl;
        }
    }
}
