// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "BenchJson.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace an::bench::json
{
namespace
{

constexpr int kInputSchemaVersion = 2;

using Json = nlohmann::json;

/// Collects parse problems so a malformed file yields every complaint at once rather than
/// stopping at the first, which makes fixing a hand-written scenario file far less tedious.
struct Reader
{
    std::vector<std::string> errors;

    void fail(const std::string& message)
    {
        errors.push_back(message);
    }

    template <typename T> T get(const Json& node, const char* key, const T& fallback)
    {
        if (!node.is_object() || !node.contains(key) || node[key].is_null())
        {
            return fallback;
        }
        try
        {
            return node[key].get<T>();
        }
        catch (const std::exception& e)
        {
            fail(std::string("field '") + key + "' has the wrong type: " + e.what());
            return fallback;
        }
    }

    /// Enums are read by name. An unknown name is an error rather than a silent default, because
    /// defaulting would turn an unreadable archived result into a plausible wrong one.
    template <typename EnumT> EnumT getEnum(const Json& node, const char* key, EnumT fallback)
    {
        const std::string text = get<std::string>(node, key, std::string(toString(fallback)));
        EnumT value = fallback;
        if (!fromString(text, value))
        {
            fail(std::string("field '") + key + "' has unknown value '" + text + "'");
            return fallback;
        }
        return value;
    }
};

Json descriptorToJson(const SampleDescriptor& d)
{
    return Json{{"width", d.width},
                {"height", d.height},
                {"channels", d.channels},
                {"sampleType", toString(d.sampleType)},
                {"layout", toString(d.layout)},
                {"byteOrder", toString(d.byteOrder)},
                {"rowStrideBytes", d.rowStrideBytes},
                {"planeStrideBytes", d.planeStrideBytes},
                {"cfaPattern", d.cfaPattern}};
}

SampleDescriptor descriptorFromJson(const Json& node, Reader& reader)
{
    SampleDescriptor d;
    d.width = reader.get<std::uint32_t>(node, "width", 0);
    d.height = reader.get<std::uint32_t>(node, "height", 0);
    d.channels = reader.get<std::uint32_t>(node, "channels", 1);
    d.sampleType = reader.getEnum(node, "sampleType", SampleType::Float32);
    d.layout = reader.getEnum(node, "layout", Layout::Planar);
    d.byteOrder = reader.getEnum(node, "byteOrder", ByteOrder::Native);
    d.rowStrideBytes = reader.get<std::uint64_t>(node, "rowStrideBytes", 0);
    d.planeStrideBytes = reader.get<std::uint64_t>(node, "planeStrideBytes", 0);
    d.cfaPattern = reader.get<std::string>(node, "cfaPattern", std::string());
    return d;
}

/// The wire name for an input source. Named rather than a ternary, so adding a third kind cannot
/// silently fold into the fallback the way it would have.
const char* inputSourceName(InputSource source)
{
    switch (source)
    {
    case InputSource::Corpus:
        return "Corpus";
    case InputSource::CorpusFrameSet:
        return "CorpusFrameSet";
    case InputSource::Synthetic:
        break;
    }
    return "Synthetic";
}

Json scenarioToJson(const ScenarioSpec& s)
{
    Json backends = Json::array();
    for (BackendId backend : s.allowedBackends)
    {
        backends.push_back(toString(backend));
    }

    return Json{{"id", s.id},
                {"group", s.group},
                {"operationVersion", s.operationVersion},
                {"parameters", s.parameters},
                {"inputSource", inputSourceName(s.inputSource)},
                {"input", descriptorToJson(s.input)},
                {"corpusId", s.corpusId},
                {"seed", s.seed},
                {"correctnessClass", toString(s.correctnessClass)},
                {"tolerance", s.tolerance},
                {"bytesProcessed", s.bytesProcessed},
                {"elementsProcessed", s.elementsProcessed},
                {"workUnit", s.workUnit},
                {"allowedBackends", backends}};
}

ScenarioSpec scenarioFromJson(const Json& node, Reader& reader)
{
    ScenarioSpec s;
    s.id = reader.get<std::string>(node, "id", std::string());
    s.group = reader.get<std::string>(node, "group", std::string());
    s.operationVersion = reader.get<std::uint32_t>(node, "operationVersion", 1);
    s.parameters = reader.get<ParameterSet>(node, "parameters", ParameterSet{});

    const std::string source = reader.get<std::string>(node, "inputSource", "Synthetic");
    if (source == "Corpus")
    {
        s.inputSource = InputSource::Corpus;
    }
    else if (source == "CorpusFrameSet")
    {
        s.inputSource = InputSource::CorpusFrameSet;
    }
    else if (source == "Synthetic")
    {
        s.inputSource = InputSource::Synthetic;
    }
    else
    {
        reader.fail("field 'inputSource' has unknown value '" + source + "'");
    }

    if (node.is_object() && node.contains("input"))
    {
        s.input = descriptorFromJson(node["input"], reader);
    }
    s.corpusId = reader.get<std::string>(node, "corpusId", std::string());
    s.seed = reader.get<std::uint64_t>(node, "seed", 0);
    s.correctnessClass = reader.getEnum(node, "correctnessClass", CorrectnessClass::BitExact);
    s.tolerance = reader.get<double>(node, "tolerance", 0.0);
    s.bytesProcessed = reader.get<std::uint64_t>(node, "bytesProcessed", 0);
    s.elementsProcessed = reader.get<std::uint64_t>(node, "elementsProcessed", 0);
    s.workUnit = reader.get<std::string>(node, "workUnit", std::string());

    if (node.is_object() && node.contains("allowedBackends") && node["allowedBackends"].is_array())
    {
        for (const Json& item : node["allowedBackends"])
        {
            BackendId backend = BackendId::Unspecified;
            if (item.is_string() && fromString(item.get<std::string>(), backend))
            {
                s.allowedBackends.push_back(backend);
            }
            else
            {
                reader.fail("allowedBackends contains an unknown backend name");
            }
        }
    }

    return s;
}

Json resultToJson(const ResultRecord& r)
{
    return Json{
        {"scenarioId", r.scenarioId},
        {"operationVersion", r.operationVersion},
        {"skipped", r.skipped},
        {"skipReason", r.skipReason},
        {"requestedBackend", toString(r.requestedBackend)},
        {"actualBackend", toString(r.actualBackend)},
        {"threadCount", r.threadCount},
        {"wallMs", r.timings.wallMs},
        {"cpuMs", r.timings.cpuMs},
        {"memory",
         Json{{"allocationCount",
               r.memory.allocationCount.has_value() ? Json(*r.memory.allocationCount) : Json(nullptr)},
              {"allocatedBytes", r.memory.allocatedBytes.has_value() ? Json(*r.memory.allocatedBytes) : Json(nullptr)},
              {"scratchHighWaterBytes",
               r.memory.scratchHighWaterBytes.has_value() ? Json(*r.memory.scratchHighWaterBytes) : Json(nullptr)},
              {"peakResidentBytes", r.memory.peakResidentBytes},
              {"retainedCacheBytes",
               r.memory.retainedCacheBytes.has_value() ? Json(*r.memory.retainedCacheBytes) : Json(nullptr)}}},
        {"outputDigest", r.outputDigest},
        {"fixtureDigest", r.fixtureDigest},
        {"scenarioDigest", r.scenarioDigest},
        {"correctnessClass", toString(r.correctnessClass)},
        {"tolerance", r.tolerance},
        {"correctnessMetric", r.correctnessMetric.has_value() ? Json(*r.correctnessMetric) : Json(nullptr)},
        {"bytesProcessed", r.bytesProcessed},
        {"elementsProcessed", r.elementsProcessed},
        {"counters", r.counters}};
}

ResultRecord resultFromJson(const Json& node, Reader& reader)
{
    ResultRecord r;
    r.scenarioId = reader.get<std::string>(node, "scenarioId", std::string());
    r.operationVersion = reader.get<std::uint32_t>(node, "operationVersion", 1);
    r.skipped = reader.get<bool>(node, "skipped", false);
    r.skipReason = reader.get<std::string>(node, "skipReason", std::string());
    r.requestedBackend = reader.getEnum(node, "requestedBackend", BackendId::Unspecified);
    r.actualBackend = reader.getEnum(node, "actualBackend", BackendId::Unspecified);
    r.threadCount = reader.get<std::uint32_t>(node, "threadCount", 1);
    r.timings.wallMs = reader.get<std::vector<double>>(node, "wallMs", {});
    r.timings.cpuMs = reader.get<std::vector<double>>(node, "cpuMs", {});

    if (node.is_object() && node.contains("memory"))
    {
        const Json& m = node["memory"];
        // An absent counter stays absent: only an engine can answer these, and a default of
        // zero would turn "this engine does not account for its allocations" into a measurement.
        const auto optionalCounter = [&](const char* name) -> std::optional<std::uint64_t> {
            if (!m.is_object() || !m.contains(name) || m[name].is_null())
            {
                return std::nullopt;
            }
            return reader.get<std::uint64_t>(m, name, 0);
        };
        r.memory.allocationCount = optionalCounter("allocationCount");
        r.memory.allocatedBytes = optionalCounter("allocatedBytes");
        r.memory.scratchHighWaterBytes = optionalCounter("scratchHighWaterBytes");
        r.memory.retainedCacheBytes = optionalCounter("retainedCacheBytes");
        r.memory.peakResidentBytes = reader.get<std::uint64_t>(m, "peakResidentBytes", 0);
    }

    r.outputDigest = reader.get<std::string>(node, "outputDigest", std::string());
    r.fixtureDigest = reader.get<std::string>(node, "fixtureDigest", std::string());
    r.scenarioDigest = reader.get<std::string>(node, "scenarioDigest", std::string());
    r.correctnessClass = reader.getEnum(node, "correctnessClass", CorrectnessClass::BitExact);
    r.tolerance = reader.get<double>(node, "tolerance", 0.0);
    if (node.is_object() && node.contains("correctnessMetric") && !node["correctnessMetric"].is_null())
    {
        r.correctnessMetric = reader.get<double>(node, "correctnessMetric", 0.0);
    }
    r.bytesProcessed = reader.get<std::uint64_t>(node, "bytesProcessed", 0);
    r.elementsProcessed = reader.get<std::uint64_t>(node, "elementsProcessed", 0);
    r.counters = reader.get<std::map<std::string, double>>(node, "counters", {});
    return r;
}

} // namespace

std::string toJson(const ScenarioSpec& scenario, bool pretty)
{
    return scenarioToJson(scenario).dump(pretty ? 2 : -1);
}

std::string toJson(const std::vector<ScenarioSpec>& scenarios, bool pretty)
{
    Json array = Json::array();
    for (const ScenarioSpec& scenario : scenarios)
    {
        array.push_back(scenarioToJson(scenario));
    }
    return Json{{"schemaVersion", kInputSchemaVersion}, {"scenarios", array}}.dump(pretty ? 2 : -1);
}

std::string toJson(const RunManifest& run, bool pretty)
{
    Json results = Json::array();
    for (const ResultRecord& record : run.results)
    {
        results.push_back(resultToJson(record));
    }

    const auto buildToJson = [](const BuildManifest& build) {
        return Json{{"commit", build.commit},
                    {"dirty", build.dirty},
                    {"compiler", build.compiler},
                    {"standardLibrary", build.standardLibrary},
                    {"linker", build.linker},
                    {"configuration", build.configuration},
                    {"flags", build.flags},
                    {"lto", build.lto},
                    {"pgo", build.pgo},
                    {"fastMath", build.fastMath},
                    {"dependencyVersions", build.dependencyVersions}};
    };

    const Json environment{{"hostId", run.environment.hostId},
                           {"cpuModel", run.environment.cpuModel},
                           {"microcodeVersion", run.environment.microcodeVersion},
                           {"physicalCores", run.environment.physicalCores},
                           {"logicalCores", run.environment.logicalCores},
                           {"numaNodes", run.environment.numaNodes},
                           {"memoryBytes", run.environment.memoryBytes},
                           {"osName", run.environment.osName},
                           {"gpuModel", run.environment.gpuModel},
                           {"driverVersion", run.environment.driverVersion},
                           {"isaCapabilities", run.environment.isaCapabilities},
                           {"powerProfile", run.environment.powerProfile},
                           {"acPower", run.environment.acPower},
                           {"otherwiseIdle", run.environment.otherwiseIdle},
                           {"cpuClockResolutionMs", run.environment.cpuClockResolutionMs},
                           {"powerPlan", run.environment.powerPlan},
                           {"affinityCores", run.environment.affinityCores},
                           {"affinityMask", run.environment.affinityMask},
                           {"cpuFrequencyMhz", Json{{"minimum", run.environment.cpuFrequencyMhz.minimum},
                                                    {"mean", run.environment.cpuFrequencyMhz.mean},
                                                    {"maximum", run.environment.cpuFrequencyMhz.maximum},
                                                    {"samples", run.environment.cpuFrequencyMhz.samples}}},
                           {"backgroundLoad", run.environment.backgroundLoad},
                           {"notes", run.environment.notes}};

    return Json{{"schemaVersion", run.schemaVersion},
                {"runnerVersion", run.runnerVersion},
                {"timestamp", run.timestamp},
                {"runnerBuild", buildToJson(run.runnerBuild)},
                {"engineBuild", buildToJson(run.build)},
                {"dependencyLockDigest", run.dependencyLockDigest},
                {"environment", environment},
                {"warmupIterations", run.warmupIterations},
                {"repetitions", run.repetitions},
                {"randomInterleaving", run.randomInterleaving},
                {"residentSampleIntervalMs", run.residentSampleIntervalMs},
                {"corpusRoot", run.corpusRoot},
                {"results", results}}
        .dump(pretty ? 2 : -1);
}

std::string toJson(const CorpusManifest& corpus, bool pretty)
{
    Json array = Json::array();
    for (const CorpusEntry& entry : corpus.entries())
    {
        array.push_back(Json{{"id", entry.id},
                             {"tags", entry.tags},
                             {"relativePath", entry.relativePath},
                             {"sha256", entry.sha256},
                             {"sizeBytes", entry.sizeBytes},
                             {"licenseOrProvenance", entry.licenseOrProvenance},
                             {"redistribution", entry.redistribution},
                             {"privacyClass", entry.privacyClass},
                             {"expected", descriptorToJson(entry.expected)},
                             {"preprocessing", entry.preprocessing},
                             {"preprocessingVersion", entry.preprocessingVersion},
                             {"goldenPath", entry.goldenPath},
                             {"goldenSha256", entry.goldenSha256},
                             {"goldenTolerance", entry.goldenTolerance},
                             {"representativeFor", entry.representativeFor}});
    }

    // Frame sets travel in their own array. A single-file dataset and a 232-frame set are read in
    // completely different ways, and folding them into one array with an optional member list would
    // make every reader decide which kind it was holding.
    Json sets = Json::array();
    for (const CorpusFrameSet& set : corpus.sets())
    {
        Json roles = Json::array();
        for (const CorpusFrameRole& role : set.roles)
        {
            Json frames = Json::array();
            for (const CorpusFrame& frame : role.frames)
            {
                frames.push_back(Json{
                    {"relativePath", frame.relativePath}, {"sha256", frame.sha256}, {"sizeBytes", frame.sizeBytes}});
            }
            roles.push_back(Json{{"role", role.role}, {"frames", frames}});
        }
        sets.push_back(Json{{"id", set.id},
                            {"tags", set.tags},
                            {"licenseOrProvenance", set.licenseOrProvenance},
                            {"redistribution", set.redistribution},
                            {"privacyClass", set.privacyClass},
                            {"expected", descriptorToJson(set.expected)},
                            {"roles", roles},
                            {"representativeFor", set.representativeFor}});
    }

    return Json{{"schemaVersion", kInputSchemaVersion}, {"datasets", array}, {"frameSets", sets}}.dump(pretty ? 2 : -1);
}

ParseResult parseScenario(const std::string& text, ScenarioSpec& out)
{
    ParseResult result;
    Json node = Json::parse(text, nullptr, false);
    if (node.is_discarded())
    {
        result.errors.push_back("input is not valid JSON");
        return result;
    }

    Reader reader;
    out = scenarioFromJson(node, reader);
    result.errors = reader.errors;
    if (result.errors.empty())
    {
        const auto validation = out.validate();
        result.errors.insert(result.errors.end(), validation.begin(), validation.end());
    }
    result.ok = result.errors.empty();
    return result;
}

ParseResult parseScenarios(const std::string& text, std::vector<ScenarioSpec>& out)
{
    ParseResult result;
    Json node = Json::parse(text, nullptr, false);
    if (node.is_discarded() || !node.is_object() || !node.contains("scenarios") || !node["scenarios"].is_array())
    {
        result.errors.push_back("input is not a valid scenario set");
        return result;
    }

    Reader reader;
    const int schemaVersion = reader.get<int>(node, "schemaVersion", 0);
    if (schemaVersion != kInputSchemaVersion)
    {
        result.errors.push_back("unsupported schemaVersion " + std::to_string(schemaVersion) +
                                "; this reader understands " + std::to_string(kInputSchemaVersion));
        return result;
    }

    std::set<std::string> ids;
    for (const Json& item : node["scenarios"])
    {
        ScenarioSpec scenario = scenarioFromJson(item, reader);
        const auto validation = scenario.validate();
        reader.errors.insert(reader.errors.end(), validation.begin(), validation.end());
        if (!scenario.id.empty() && !ids.insert(scenario.id).second)
        {
            reader.fail("duplicate scenario id '" + scenario.id + "'");
        }
        out.push_back(std::move(scenario));
    }
    result.errors = reader.errors;
    result.ok = result.errors.empty();
    return result;
}

ParseResult parseRun(const std::string& text, RunManifest& out)
{
    ParseResult result;
    Json node = Json::parse(text, nullptr, false);
    if (node.is_discarded() || !node.is_object())
    {
        result.errors.push_back("input is not valid JSON");
        return result;
    }

    Reader reader;
    out = RunManifest{};
    out.schemaVersion = reader.get<int>(node, "schemaVersion", 0);
    if (out.schemaVersion != RunManifest::kSchemaVersion)
    {
        // Refuse rather than guess: field meanings are not stable across schema versions.
        result.errors.push_back("unsupported schemaVersion " + std::to_string(out.schemaVersion) +
                                "; this reader understands " + std::to_string(RunManifest::kSchemaVersion));
        return result;
    }

    out.runnerVersion = reader.get<std::string>(node, "runnerVersion", std::string());
    out.timestamp = reader.get<std::string>(node, "timestamp", std::string());
    out.dependencyLockDigest = reader.get<std::string>(node, "dependencyLockDigest", std::string());

    const auto parseBuild = [&](const Json& b, BuildManifest& build) {
        for (const char* field : {"commit", "dirty", "compiler", "standardLibrary", "linker", "configuration", "flags",
                                  "lto", "pgo", "fastMath", "dependencyVersions"})
        {
            if (!b.is_object() || !b.contains(field) || b[field].is_null())
            {
                reader.fail(std::string("build provenance has no field '") + field + "'");
            }
        }
        build.commit = reader.get<std::string>(b, "commit", std::string());
        build.dirty = reader.get<bool>(b, "dirty", false);
        build.compiler = reader.get<std::string>(b, "compiler", std::string());
        build.standardLibrary = reader.get<std::string>(b, "standardLibrary", std::string());
        build.linker = reader.get<std::string>(b, "linker", std::string());
        build.configuration = reader.get<std::string>(b, "configuration", std::string());
        build.flags = reader.get<std::string>(b, "flags", std::string());
        build.lto = reader.get<bool>(b, "lto", false);
        build.pgo = reader.get<bool>(b, "pgo", false);
        build.fastMath = reader.get<bool>(b, "fastMath", false);
        build.dependencyVersions = reader.get<std::map<std::string, std::string>>(b, "dependencyVersions", {});
    };

    if (node.contains("runnerBuild"))
    {
        parseBuild(node["runnerBuild"], out.runnerBuild);
    }
    if (node.contains("engineBuild"))
    {
        parseBuild(node["engineBuild"], out.build);
    }

    if (node.contains("environment"))
    {
        const Json& e = node["environment"];
        out.environment.hostId = reader.get<std::string>(e, "hostId", std::string());
        out.environment.cpuModel = reader.get<std::string>(e, "cpuModel", std::string());
        out.environment.microcodeVersion = reader.get<std::string>(e, "microcodeVersion", std::string());
        out.environment.physicalCores = reader.get<std::uint32_t>(e, "physicalCores", 0);
        out.environment.logicalCores = reader.get<std::uint32_t>(e, "logicalCores", 0);
        out.environment.numaNodes = reader.get<std::uint32_t>(e, "numaNodes", 0);
        out.environment.memoryBytes = reader.get<std::uint64_t>(e, "memoryBytes", 0);
        out.environment.osName = reader.get<std::string>(e, "osName", std::string());
        out.environment.gpuModel = reader.get<std::string>(e, "gpuModel", std::string());
        out.environment.driverVersion = reader.get<std::string>(e, "driverVersion", std::string());
        out.environment.isaCapabilities = reader.get<std::vector<std::string>>(e, "isaCapabilities", {});
        out.environment.powerProfile = reader.get<std::string>(e, "powerProfile", std::string());
        out.environment.acPower = reader.get<bool>(e, "acPower", true);
        out.environment.otherwiseIdle = reader.get<bool>(e, "otherwiseIdle", false);
        out.environment.cpuClockResolutionMs = reader.get<double>(e, "cpuClockResolutionMs", 0.0);
        out.environment.powerPlan = reader.get<std::string>(e, "powerPlan", std::string());
        out.environment.affinityCores = reader.get<std::uint32_t>(e, "affinityCores", 0);
        out.environment.affinityMask = reader.get<std::string>(e, "affinityMask", std::string());
        if (e.contains("cpuFrequencyMhz") && e["cpuFrequencyMhz"].is_object())
        {
            const Json& frequency = e["cpuFrequencyMhz"];
            out.environment.cpuFrequencyMhz.minimum = reader.get<double>(frequency, "minimum", 0.0);
            out.environment.cpuFrequencyMhz.mean = reader.get<double>(frequency, "mean", 0.0);
            out.environment.cpuFrequencyMhz.maximum = reader.get<double>(frequency, "maximum", 0.0);
            out.environment.cpuFrequencyMhz.samples = reader.get<std::uint32_t>(frequency, "samples", 0);
        }
        out.environment.backgroundLoad = reader.get<double>(e, "backgroundLoad", 0.0);
        out.environment.notes = reader.get<std::string>(e, "notes", std::string());
    }

    out.warmupIterations = reader.get<std::uint32_t>(node, "warmupIterations", 0);
    out.repetitions = reader.get<std::uint32_t>(node, "repetitions", 0);
    out.randomInterleaving = reader.get<bool>(node, "randomInterleaving", false);
    out.residentSampleIntervalMs = reader.get<std::uint32_t>(node, "residentSampleIntervalMs", 0);
    out.corpusRoot = reader.get<std::string>(node, "corpusRoot", std::string());

    if (node.contains("results") && node["results"].is_array())
    {
        for (const Json& item : node["results"])
        {
            out.results.push_back(resultFromJson(item, reader));
        }
    }

    result.errors = reader.errors;
    if (result.errors.empty())
    {
        const auto validation = out.validate();
        result.errors.insert(result.errors.end(), validation.begin(), validation.end());
    }
    result.ok = result.errors.empty();
    return result;
}

ParseResult parseCorpus(const std::string& text, CorpusManifest& out)
{
    ParseResult result;
    Json node = Json::parse(text, nullptr, false);
    if (node.is_discarded() || !node.is_object() || !node.contains("datasets") || !node["datasets"].is_array())
    {
        result.errors.push_back("input is not a valid corpus manifest");
        return result;
    }

    Reader reader;
    const int schemaVersion = reader.get<int>(node, "schemaVersion", 0);
    if (schemaVersion != kInputSchemaVersion)
    {
        result.errors.push_back("unsupported schemaVersion " + std::to_string(schemaVersion) +
                                "; this reader understands " + std::to_string(kInputSchemaVersion));
        return result;
    }
    for (const Json& item : node["datasets"])
    {
        CorpusEntry entry;
        entry.id = reader.get<std::string>(item, "id", std::string());
        entry.tags = reader.get<std::vector<std::string>>(item, "tags", {});
        entry.relativePath = reader.get<std::string>(item, "relativePath", std::string());
        entry.sha256 = reader.get<std::string>(item, "sha256", std::string());
        entry.sizeBytes = reader.get<std::uint64_t>(item, "sizeBytes", 0);
        entry.licenseOrProvenance = reader.get<std::string>(item, "licenseOrProvenance", std::string());
        entry.redistribution = reader.get<std::string>(item, "redistribution", std::string());
        entry.privacyClass = reader.get<std::string>(item, "privacyClass", std::string());
        if (item.is_object() && item.contains("expected"))
        {
            entry.expected = descriptorFromJson(item["expected"], reader);
        }
        entry.preprocessing = reader.get<std::string>(item, "preprocessing", std::string());
        entry.preprocessingVersion = reader.get<std::string>(item, "preprocessingVersion", std::string());
        entry.goldenPath = reader.get<std::string>(item, "goldenPath", std::string());
        entry.goldenSha256 = reader.get<std::string>(item, "goldenSha256", std::string());
        entry.goldenTolerance = reader.get<double>(item, "goldenTolerance", 0.0);
        entry.representativeFor = reader.get<std::vector<std::string>>(item, "representativeFor", {});
        out.add(std::move(entry));
    }

    // Absent rather than required: a manifest that names only single-file datasets is well formed,
    // and every manifest written before frame sets existed is one.
    if (node.contains("frameSets") && node["frameSets"].is_array())
    {
        for (const Json& item : node["frameSets"])
        {
            CorpusFrameSet set;
            set.id = reader.get<std::string>(item, "id", std::string());
            set.tags = reader.get<std::vector<std::string>>(item, "tags", {});
            set.licenseOrProvenance = reader.get<std::string>(item, "licenseOrProvenance", std::string());
            set.redistribution = reader.get<std::string>(item, "redistribution", std::string());
            set.privacyClass = reader.get<std::string>(item, "privacyClass", std::string());
            if (item.is_object() && item.contains("expected"))
            {
                set.expected = descriptorFromJson(item["expected"], reader);
            }
            set.representativeFor = reader.get<std::vector<std::string>>(item, "representativeFor", {});

            if (item.is_object() && item.contains("roles") && item["roles"].is_array())
            {
                for (const Json& roleItem : item["roles"])
                {
                    CorpusFrameRole role;
                    role.role = reader.get<std::string>(roleItem, "role", std::string());
                    if (roleItem.is_object() && roleItem.contains("frames") && roleItem["frames"].is_array())
                    {
                        for (const Json& frameItem : roleItem["frames"])
                        {
                            CorpusFrame frame;
                            frame.relativePath = reader.get<std::string>(frameItem, "relativePath", std::string());
                            frame.sha256 = reader.get<std::string>(frameItem, "sha256", std::string());
                            frame.sizeBytes = reader.get<std::uint64_t>(frameItem, "sizeBytes", 0);
                            role.frames.push_back(std::move(frame));
                        }
                    }
                    set.roles.push_back(std::move(role));
                }
            }
            out.addSet(std::move(set));
        }
    }

    result.errors = reader.errors;
    if (result.errors.empty())
    {
        const auto validation = out.validate();
        result.errors.insert(result.errors.end(), validation.begin(), validation.end());
    }
    result.ok = result.errors.empty();
    return result;
}

ParseResult readRunFile(const std::string& path, RunManifest& out)
{
    ParseResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        result.errors.push_back("cannot open '" + path + "'");
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseRun(buffer.str(), out);
}

ParseResult readCorpusFile(const std::string& path, CorpusManifest& out)
{
    ParseResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        result.errors.push_back("cannot open '" + path + "'");
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseCorpus(buffer.str(), out);
}

ParseResult readScenariosFile(const std::string& path, std::vector<ScenarioSpec>& out)
{
    ParseResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        result.errors.push_back("cannot open '" + path + "'");
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseScenarios(buffer.str(), out);
}

namespace
{

/// Write `text` to `path` through a pending file beside it, then rename it into place.
///
/// An interrupted write cannot leave a truncated file that later parses as a valid but incomplete
/// result. This is the same atomicity rule the engine's generated artifacts follow.
bool writeTextAtomically(const std::string& path, const std::string& text, std::string& error)
{
    const std::string pending = path + ".pending";
    {
        std::ofstream file(pending, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "cannot open '" + pending + "' for writing";
            return false;
        }
        file << text;
        if (!file)
        {
            error = "write to '" + pending + "' failed";
            return false;
        }
    }

    std::error_code code;
    std::filesystem::rename(pending, path, code);
    if (code)
    {
        // rename() across an existing destination fails on some platforms; remove and retry once.
        std::filesystem::remove(path, code);
        std::filesystem::rename(pending, path, code);
    }
    if (code)
    {
        error = "cannot move '" + pending + "' into place: " + code.message();
        std::filesystem::remove(pending, code);
        return false;
    }
    return true;
}

} // namespace

bool writeRunFile(const std::string& path, const RunManifest& run, std::string& error)
{
    return writeTextAtomically(path, toJson(run), error);
}

bool writeScenariosFile(const std::string& path, const std::vector<ScenarioSpec>& scenarios, std::string& error)
{
    return writeTextAtomically(path, toJson(scenarios), error);
}

} // namespace an::bench::json
