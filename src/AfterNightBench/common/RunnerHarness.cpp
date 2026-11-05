// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "RunnerHarness.h"

#include "BenchJson.h"
#include "Digest.h"
#include "CorpusFixtures.h"
#include "Environment.h"
#include "HostPolicy.h"
#include "ResidentMemory.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#define AFTERNIGHT_BENCH_POPEN _popen
#define AFTERNIGHT_BENCH_PCLOSE _pclose
#else
#include <time.h>
#include <unistd.h>
#define AFTERNIGHT_BENCH_POPEN popen
#define AFTERNIGHT_BENCH_PCLOSE pclose
#endif

namespace an::bench
{
namespace
{

constexpr const char* kUnknownHost = "unknown-host";

#if defined(_WIN32)
constexpr const char* kOsName = "Windows";
constexpr const char* kHostVariable = "COMPUTERNAME";
#else
constexpr const char* kOsName = "Linux";
constexpr const char* kHostVariable = "HOSTNAME";
#endif

/// Read the whole output of `command`, or an empty string when it cannot be run.
///
/// Used only for the two git queries that identify a run. A benchmark runner is a development
/// tool, so spawning a process to answer a question about the checkout is acceptable here in a way
/// it would not be inside the engine.
std::string captureCommand(const char* command)
{
    std::FILE* pipe = AFTERNIGHT_BENCH_POPEN(command, "r");
    if (pipe == nullptr)
    {
        return {};
    }

    std::string output;
    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        output += buffer.data();
    }
    AFTERNIGHT_BENCH_PCLOSE(pipe);
    return output;
}

std::string trimmed(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool parseCount(const std::string& text, std::uint32_t& out)
{
    std::uint32_t parsed = 0;
    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto result = std::from_chars(first, last, parsed);
    if (first == last || result.ec != std::errc{} || result.ptr != last)
    {
        return false;
    }
    out = parsed;
    return true;
}

/// A fraction strictly between zero and one, spelled as a decimal number and nothing else.
bool parseInteriorFraction(const std::string& text, double& out)
{
    double parsed = 0.0;
    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto result = std::from_chars(first, last, parsed);
    if (first == last || result.ec != std::errc{} || result.ptr != last || !(parsed > 0.0 && parsed < 1.0))
    {
        return false;
    }
    out = parsed;
    return true;
}

std::string detectCpuModel()
{
#if defined(_WIN32)
    return trimmed(environmentValue("PROCESSOR_IDENTIFIER"));
#else
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line))
    {
        const auto colon = line.find(':');
        if (colon != std::string::npos && line.substr(0, colon).find("model name") != std::string::npos)
        {
            return trimmed(line.substr(colon + 1));
        }
    }
    return {};
#endif
}

std::string detectMicrocodeVersion()
{
#if defined(_WIN32)
    std::array<unsigned char, 32> revision{};
    DWORD bytes = static_cast<DWORD>(revision.size());
    const LSTATUS status = RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                        "Update Revision", RRF_RT_REG_BINARY, nullptr, revision.data(), &bytes);
    if (status != ERROR_SUCCESS || bytes == 0)
    {
        return {};
    }
    std::ostringstream text;
    text << "0x" << std::hex << std::setfill('0');
    for (DWORD index = 0; index < bytes; ++index)
    {
        text << std::setw(2) << static_cast<unsigned int>(revision[index]);
    }
    return text.str();
#else
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line))
    {
        const auto colon = line.find(':');
        if (colon != std::string::npos && trimmed(line.substr(0, colon)) == "microcode")
        {
            return trimmed(line.substr(colon + 1));
        }
    }
    return {};
#endif
}

std::uint32_t detectNumaNodes()
{
#if defined(_WIN32)
    ULONG highestNode = 0;
    return GetNumaHighestNodeNumber(&highestNode) ? static_cast<std::uint32_t>(highestNode + 1u) : 0u;
#else
    std::ifstream online("/sys/devices/system/node/online");
    std::string ranges;
    if (!(online >> ranges))
    {
        return 0u;
    }
    std::uint32_t count = 0u;
    std::istringstream list(ranges);
    std::string range;
    while (std::getline(list, range, ','))
    {
        const auto dash = range.find('-');
        if (dash == std::string::npos)
        {
            ++count;
            continue;
        }
        const auto first = static_cast<std::uint32_t>(std::strtoul(range.substr(0, dash).c_str(), nullptr, 10));
        const auto last = static_cast<std::uint32_t>(std::strtoul(range.substr(dash + 1).c_str(), nullptr, 10));
        if (last >= first)
        {
            count += last - first + 1u;
        }
    }
    return count;
#endif
}

std::uint32_t detectPhysicalCores()
{
#if defined(_WIN32)
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0)
    {
        return 0;
    }
    std::vector<std::uint8_t> buffer(bytes);
    auto* information = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, information, &bytes))
    {
        return 0;
    }
    std::uint32_t cores = 0;
    std::size_t offset = 0;
    while (offset < bytes)
    {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (entry->Relationship == RelationProcessorCore)
        {
            ++cores;
        }
        if (entry->Size == 0)
        {
            return 0;
        }
        offset += entry->Size;
    }
    return cores;
#else
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::set<std::pair<std::string, std::string>> cores;
    std::string physicalId;
    std::string coreId;
    std::string line;
    auto commitCore = [&]() {
        if (!physicalId.empty() && !coreId.empty())
        {
            cores.emplace(physicalId, coreId);
        }
        physicalId.clear();
        coreId.clear();
    };
    while (std::getline(cpuInfo, line))
    {
        if (line.empty())
        {
            commitCore();
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        const std::string key = trimmed(line.substr(0, colon));
        if (key == "physical id")
        {
            physicalId = trimmed(line.substr(colon + 1));
        }
        else if (key == "core id")
        {
            coreId = trimmed(line.substr(colon + 1));
        }
    }
    commitCore();
    return cores.empty() ? std::thread::hardware_concurrency() : static_cast<std::uint32_t>(cores.size());
#endif
}

std::uint64_t detectMemoryBytes()
{
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || pageSize <= 0)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
#endif
}

bool detectAcPower()
{
#if defined(_WIN32)
    SYSTEM_POWER_STATUS status{};
    return !GetSystemPowerStatus(&status) || status.ACLineStatus != 0;
#else
    for (const char* path : {"/sys/class/power_supply/AC/online", "/sys/class/power_supply/ACAD/online"})
    {
        std::ifstream state(path);
        int online = 0;
        if (state >> online)
        {
            return online != 0;
        }
    }
    return true;
#endif
}

/// Granularity of the process CPU clock, in milliseconds.
///
/// Asked of the operating system rather than assumed. On Windows the process clock advances by the
/// system timer increment, which is what `GetSystemTimeAdjustment` reports in 100 ns units; on
/// Linux `clock_getres` answers for the process CPU clock directly. A scenario whose wall time does
/// not comfortably exceed this has no usable CPU number, which is why the run records it.
double detectCpuClockResolutionMs()
{
#if defined(_WIN32)
    DWORD adjustment = 0;
    DWORD increment = 0;
    BOOL disabled = FALSE;
    if (GetSystemTimeAdjustment(&adjustment, &increment, &disabled) == 0 || increment == 0)
    {
        return 0.0;
    }
    return static_cast<double>(increment) / 10000.0;
#else
    timespec resolution{};
    if (clock_getres(CLOCK_PROCESS_CPUTIME_ID, &resolution) != 0)
    {
        return 0.0;
    }
    return static_cast<double>(resolution.tv_sec) * 1000.0 + static_cast<double>(resolution.tv_nsec) / 1000000.0;
#endif
}

/// Walk the corpus and report, without measuring anything.
///
/// Builds its own fixture source rather than borrowing the adapter's: verification needs the
/// manifest and the root, not an engine, and running it before `prepare` means a manifest that is
/// wrong is reported by the check built to explain it rather than by an adapter refusing to start.
int verifyCorpusAndReport(const RunOptions& options)
{
    if (options.corpusManifest.empty())
    {
        std::cerr << "--verify-corpus needs --corpus-manifest\n";
        return 3;
    }

    CorpusManifest manifest;
    if (const auto parsed = json::readCorpusFile(options.corpusManifest, manifest); !parsed)
    {
        for (const std::string& message : parsed.errors)
        {
            std::cerr << "corpus manifest: " << message << '\n';
        }
        return 3;
    }

    if (options.corpusRoot.empty())
    {
        std::cerr << "--verify-corpus needs a corpus root, from --corpus-root or " << kCorpusRootVariable << '\n';
        return 3;
    }

    CompositeFixtureSource source;
    source.useCorpus(manifest, options.corpusRoot);
    const CorpusVerification report = verifyCorpus(manifest, source);

    std::cout << "corpus root: " << options.corpusRoot << '\n'
              << "datasets: " << report.datasetsPresent << " present, " << report.datasetsAbsent << " absent\n"
              << "frame sets: " << report.setsPresent << " present, " << report.setsAbsent << " absent\n"
              << "frames resolved: " << report.framesResolved << '\n'
              << "bytes resolved: " << report.bytesResolved << '\n';
    for (const std::string& problem : report.problems)
    {
        std::cout << "  " << problem << '\n';
    }
    std::cout << (report.ok ? "corpus ok\n" : "corpus incomplete\n");
    return report.ok ? 0 : 1;
}

/// The standard library this runner was built against.
///
/// Two binaries built against different standard libraries are not comparable even when the
/// compiler matches: the containers, the allocator and the maths headers all differ, and the run
/// protocol lists this among the fields a publishable run must carry.
std::string standardLibraryName()
{
#if defined(_MSVC_STL_VERSION)
    return "MSVC STL " + std::to_string(_MSVC_STL_VERSION) + " update " + std::to_string(_MSVC_STL_UPDATE);
#elif defined(_LIBCPP_VERSION)
    return "libc++ " + std::to_string(_LIBCPP_VERSION);
#elif defined(_GLIBCXX_RELEASE)
    return "libstdc++ " + std::to_string(_GLIBCXX_RELEASE) + " (" + std::to_string(__GLIBCXX__) + ")";
#else
    return "unknown";
#endif
}

std::string compilerName()
{
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER);
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown";
#endif
}

std::string digestFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return {};
    }
    Sha256 digest;
    std::array<char, 16384> buffer{};
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
    {
        digest.update(buffer.data(), static_cast<std::size_t>(file.gcount()));
    }
    return file.eof() ? digest.hex() : std::string{};
}

/// The linker family. Named from the toolchain rather than probed, because a linked binary cannot
/// ask what linked it; what matters for comparability is that two runs used the same one.
std::string linkerName()
{
#if defined(_MSC_VER) && !defined(__clang__)
    return "MSVC link " + std::to_string(_MSC_FULL_VER);
#elif defined(__clang__)
    return "clang driver " __clang_version__;
#else
    return "unknown";
#endif
}

/// The build settings that move a measurement, as a short stable summary.
///
/// Not the whole command line, which is long, mostly irrelevant and unavailable at run time. These
/// four change numbers: the vector width the compiler was allowed to emit, the language mode, the
/// floating-point contract, and whether this is an optimized build at all. A run that differs from
/// another in any of them is measuring a different program.
std::string compilerFlagsSummary()
{
    std::string summary = "arch=";
#if defined(__AVX512F__)
    summary += "avx512";
#elif defined(__AVX2__)
    summary += "avx2";
#elif defined(__AVX__)
    summary += "avx";
#elif defined(_M_X64) || defined(__x86_64__)
    summary += "sse2";
#else
    summary += "unknown";
#endif

#if defined(_MSVC_LANG)
    summary += " cpp=" + std::to_string(_MSVC_LANG);
#else
    summary += " cpp=" + std::to_string(__cplusplus);
#endif

#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
    summary += " fastmath=on";
#else
    summary += " fastmath=off";
#endif

#if defined(NDEBUG)
    summary += " ndebug=on";
#else
    summary += " ndebug=off";
#endif
    return summary;
}

/// Instruction-set capabilities, measured from the processor.
///
/// Asked of the CPU rather than inferred from the flags this binary was built with, because those
/// are different claims: a run may execute on a machine that offers more than the build targeted,
/// and what a kernel could dispatch to is a property of the machine. It is recorded for exactly that
/// reason: two hosts can expose different AVX-512 implementations.
std::vector<std::string> detectIsaCapabilities()
{
    std::vector<std::string> found;

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#if defined(_MSC_VER)
    auto cpuid = [](int leaf, int subleaf, int regs[4]) { __cpuidex(regs, leaf, subleaf); };
    int regs[4] = {0, 0, 0, 0};
    cpuid(0, 0, regs);
    const int highestLeaf = regs[0];
    bool avxStateEnabled = false;
    bool avx512StateEnabled = false;

    if (highestLeaf >= 1)
    {
        cpuid(1, 0, regs);
        const std::uint32_t ecx = static_cast<std::uint32_t>(regs[2]);
        const std::uint32_t edx = static_cast<std::uint32_t>(regs[3]);
        if ((edx & (1u << 26u)) != 0u)
        {
            found.emplace_back("SSE2");
        }
        if ((ecx & (1u << 20u)) != 0u)
        {
            found.emplace_back("SSE4.2");
        }

        const bool osSavesExtendedState = (ecx & (1u << 27u)) != 0u;
        if (osSavesExtendedState)
        {
            const std::uint64_t enabledState = _xgetbv(0);
            avxStateEnabled = (enabledState & 0x6u) == 0x6u;
            avx512StateEnabled = (enabledState & 0xE6u) == 0xE6u;
        }
        const bool avxAvailable = (ecx & (1u << 28u)) != 0u && avxStateEnabled;
        if ((ecx & (1u << 12u)) != 0u && avxAvailable)
        {
            found.emplace_back("FMA");
        }
        if (avxAvailable)
        {
            found.emplace_back("AVX");
        }
    }
    if (highestLeaf >= 7)
    {
        cpuid(7, 0, regs);
        const std::uint32_t ebx = static_cast<std::uint32_t>(regs[1]);
        if ((ebx & (1u << 5u)) != 0u && avxStateEnabled)
        {
            found.emplace_back("AVX2");
        }
        const bool avx512Available = (ebx & (1u << 16u)) != 0u && avx512StateEnabled;
        if (avx512Available)
        {
            found.emplace_back("AVX512F");
        }
        if ((ebx & (1u << 17u)) != 0u && avx512Available)
        {
            found.emplace_back("AVX512DQ");
        }
        if ((ebx & (1u << 30u)) != 0u && avx512Available)
        {
            found.emplace_back("AVX512BW");
        }
        if ((ebx & (1u << 31u)) != 0u && avx512Available)
        {
            found.emplace_back("AVX512VL");
        }
    }
#else
    // The compiler's own runtime detection, which is what a dispatching kernel would use here.
    // Each feature name has to reach __builtin_cpu_supports as a literal - Clang rejects one
    // read out of a table at run time - so these are written out rather than looped over.
    __builtin_cpu_init();
    const auto record = [&found](bool supported, const char* name) {
        if (supported)
        {
            found.emplace_back(name);
        }
    };
    record(__builtin_cpu_supports("sse2"), "SSE2");
    record(__builtin_cpu_supports("sse4.2"), "SSE4.2");
    record(__builtin_cpu_supports("fma"), "FMA");
    record(__builtin_cpu_supports("avx"), "AVX");
    record(__builtin_cpu_supports("avx2"), "AVX2");
    record(__builtin_cpu_supports("avx512f"), "AVX512F");
    record(__builtin_cpu_supports("avx512dq"), "AVX512DQ");
    record(__builtin_cpu_supports("avx512bw"), "AVX512BW");
    record(__builtin_cpu_supports("avx512vl"), "AVX512VL");
#endif
#endif

    return found;
}

} // namespace

std::string runnerUsage(const std::string& executable)
{
    return "usage: " + executable +
           " --out <run.json>\n"
           "       [--scenarios <scenarios.json>] [--dump-scenarios <scenarios.json>]\n"
           "       [--corpus-root <dir>] [--corpus-manifest <corpus.json>]\n"
           "       [--verify-corpus]\n"
           "       [--host-id <id>] [--commit <sha>] [--dirty | --clean]\n"
           "       [--engine-commit <sha>] [--engine-dirty | --engine-clean]\n"
           "       [--engine-lto | --engine-no-lto]\n"
           "       [--dependency-lock-digest <sha256>] [--timestamp <iso8601>]\n"
           "       [--repetitions <n>] [--warmup <n>] [--threads <n>]\n"
           "       [--backend <Scalar|Sse2|Sse42|Avx|Avx2|Avx512|Neon|Gpu>]\n"
           "       [--power-profile <name>] [--otherwise-idle] [--notes <text>]\n"
           "       [--cancel-at <fraction>] [--keep-pipeline-scratch <dir>] [--dry-run]\n";
}

bool parseRunOptions(int argc, char** argv, RunOptions& out, std::string& error)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        const bool hasValue = index + 1 < argc;

        auto takeValue = [&](std::string& destination) {
            if (!hasValue)
            {
                error = argument + " needs a value";
                return false;
            }
            destination = argv[++index];
            return true;
        };

        if (argument == "--out")
        {
            if (!takeValue(out.outputPath))
            {
                return false;
            }
        }
        else if (argument == "--scenarios")
        {
            if (!takeValue(out.scenarioPath))
            {
                return false;
            }
        }
        else if (argument == "--dump-scenarios")
        {
            if (!takeValue(out.scenarioDumpPath))
            {
                return false;
            }
        }
        else if (argument == "--corpus-root")
        {
            if (!takeValue(out.corpusRoot))
            {
                return false;
            }
        }
        else if (argument == "--verify-corpus")
        {
            out.verifyCorpusOnly = true;
        }
        else if (argument == "--corpus-manifest")
        {
            if (!takeValue(out.corpusManifest))
            {
                return false;
            }
        }
        else if (argument == "--host-id")
        {
            if (!takeValue(out.hostId))
            {
                return false;
            }
        }
        else if (argument == "--commit")
        {
            if (!takeValue(out.commit))
            {
                return false;
            }
        }
        else if (argument == "--engine-commit")
        {
            if (!takeValue(out.engineCommit))
            {
                return false;
            }
        }
        else if (argument == "--dependency-lock-digest")
        {
            if (!takeValue(out.dependencyLockDigest))
            {
                return false;
            }
        }
        else if (argument == "--timestamp")
        {
            if (!takeValue(out.timestamp))
            {
                return false;
            }
        }
        else if (argument == "--power-profile")
        {
            if (!takeValue(out.powerProfile))
            {
                return false;
            }
        }
        else if (argument == "--notes")
        {
            if (!takeValue(out.notes))
            {
                return false;
            }
        }
        else if (argument == "--keep-pipeline-scratch")
        {
            if (!takeValue(out.keptPipelineScratch))
            {
                return false;
            }
        }
        else if (argument == "--repetitions" || argument == "--warmup" || argument == "--threads")
        {
            std::string value;
            if (!takeValue(value))
            {
                return false;
            }
            std::uint32_t count = 0;
            if (!parseCount(value, count))
            {
                error = argument + " needs a non-negative whole number, not '" + value + "'";
                return false;
            }
            if (argument == "--threads" && count == 0u)
            {
                error = "--threads must be at least 1 when it is stated";
                return false;
            }
            if (argument == "--repetitions")
            {
                out.repetitions = count;
            }
            else if (argument == "--warmup")
            {
                out.warmupIterations = count;
            }
            else
            {
                out.maxThreads = count;
            }
        }
        else if (argument == "--cancel-at")
        {
            std::string value;
            if (!takeValue(value))
            {
                return false;
            }
            double fraction = 0.0;
            if (!parseInteriorFraction(value, fraction))
            {
                error = "--cancel-at needs a fraction of an operation strictly between 0 and 1, not '" + value + "'";
                return false;
            }
            out.cancelAtFraction = fraction;
        }
        else if (argument == "--backend")
        {
            std::string value;
            if (!takeValue(value))
            {
                return false;
            }
            BackendId backend = BackendId::Unspecified;
            if (!fromString(value, backend) ||
                (backend != BackendId::Scalar && backend != BackendId::Sse2 && backend != BackendId::Sse42 &&
                 backend != BackendId::Avx && backend != BackendId::Avx2 && backend != BackendId::Avx512 &&
                 backend != BackendId::Neon && backend != BackendId::Gpu))
            {
                error =
                    "--backend needs one of Scalar, Sse2, Sse42, Avx, Avx2, Avx512, Neon, or Gpu, not '" + value + "'";
                return false;
            }
            out.forcedBackend = backend;
        }
        else if (argument == "--dirty" || argument == "--clean")
        {
            out.dirty = argument == "--dirty";
            out.dirtyProvided = true;
        }
        else if (argument == "--engine-dirty" || argument == "--engine-clean")
        {
            out.engineDirty = argument == "--engine-dirty";
            out.engineDirtyProvided = true;
        }
        else if (argument == "--engine-lto" || argument == "--engine-no-lto")
        {
            out.engineLto = argument == "--engine-lto";
        }
        else if (argument == "--otherwise-idle")
        {
            out.otherwiseIdle = true;
        }
        else if (argument == "--dry-run")
        {
            out.dryRun = true;
        }
        else
        {
            error = "unknown option '" + argument + "'";
            return false;
        }
    }

    // A corpus check writes no result file, so requiring one would make the check awkward to
    // run at exactly the moment it is most wanted: before a measurement has been set up.
    if (out.outputPath.empty() && !out.verifyCorpusOnly)
    {
        error = "--out is required";
        return false;
    }
    if (!out.dryRun && out.repetitions == 0)
    {
        error = "--repetitions 0 measures nothing; use --dry-run to prove the protocol instead";
        return false;
    }
    return true;
}

std::string detectHostId()
{
    const std::string host = trimmed(environmentValue(kHostVariable));
    return host.empty() ? kUnknownHost : host;
}

std::string currentTimestamp()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &now) != 0)
    {
        return {};
    }
#else
    if (gmtime_r(&now, &utc) == nullptr)
    {
        return {};
    }
#endif
    std::array<char, 32> buffer{};
    const std::size_t written = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(buffer.data(), written);
}

std::string detectCommit(bool& dirty)
{
    const std::string commit = trimmed(captureCommand("git rev-parse HEAD 2>&1"));
    // A failure message is not a commit. Any real answer is 40 hex characters, so anything else -
    // "fatal: not a git repository", an empty pipe - is reported as no commit at all rather than
    // recorded as though it identified something.
    if (commit.size() != 40 || commit.find_first_not_of("0123456789abcdef") != std::string::npos)
    {
        return {};
    }
    dirty = !trimmed(captureCommand("git status --porcelain 2>&1")).empty();
    return commit;
}

void resolveRunOptions(RunOptions& options)
{
    // The real corpus is host-local, so the root usually comes from the environment rather than
    // from every command line.
    options.corpusRoot = resolveCorpusRoot(options.corpusRoot);
    if (options.hostId.empty())
    {
        options.hostId = detectHostId();
    }
    if (options.timestamp.empty())
    {
        options.timestamp = currentTimestamp();
    }
    if (options.commit.empty())
    {
        bool detectedDirty = false;
        options.commit = detectCommit(detectedDirty);
        if (!options.dirtyProvided)
        {
            options.dirty = detectedDirty;
        }
    }
    if (options.engineCommit.empty())
    {
        options.engineCommit = options.commit;
    }
    if (!options.engineDirtyProvided)
    {
        options.engineDirty = options.dirty;
    }
    if (options.dependencyLockDigest.empty())
    {
        options.dependencyLockDigest = digestFile("docs/dev/dependency-lock.json");
    }
}

RunManifest buildRun(const RunOptions& options, const std::vector<ScenarioSpec>& scenarios, EngineAdapter& adapter)
{
    RunManifest run;
    run.runnerVersion = adapter.runnerVersion();
    run.timestamp = options.timestamp;
    run.corpusRoot = options.corpusRoot;
    run.warmupIterations = options.dryRun ? 0 : options.warmupIterations;
    run.repetitions = options.dryRun ? 0 : options.repetitions;
    run.randomInterleaving = options.randomInterleaving;
    run.residentSampleIntervalMs = kResidentSampleIntervalMs;

    run.runnerBuild.commit = options.commit;
    run.runnerBuild.dirty = options.dirty;
    // Properties of this runner binary, which is engine-neutral, so they are filled in here rather
    // than by an adapter. The adapter is asked afterwards and may still override any of them.
    run.runnerBuild.compiler = compilerName();
    run.runnerBuild.standardLibrary = standardLibraryName();
    run.runnerBuild.linker = linkerName();
    run.runnerBuild.flags = compilerFlagsSummary();
#if defined(NDEBUG)
    run.runnerBuild.configuration = "Release";
#else
    run.runnerBuild.configuration = "Debug";
#endif
    run.runnerBuild.fastMath = run.runnerBuild.flags.find("fastmath=on") != std::string::npos;
    run.runnerBuild.dependencyVersions["EngineBenchCommon"] = "schema " + std::to_string(RunManifest::kSchemaVersion);

    run.build = run.runnerBuild;
    run.build.commit = options.engineCommit;
    run.build.dirty = options.engineDirty;
    adapter.describeEngine(run.build);
    if (options.engineLto.has_value())
        run.build.lto = *options.engineLto;
    run.dependencyLockDigest = options.dependencyLockDigest;

    run.environment.hostId = options.hostId;
    run.environment.osName = kOsName;
    run.environment.logicalCores = std::thread::hardware_concurrency();
    run.environment.physicalCores = detectPhysicalCores();
    run.environment.cpuModel = detectCpuModel();
    run.environment.microcodeVersion = detectMicrocodeVersion();
    run.environment.numaNodes = detectNumaNodes();
    run.environment.memoryBytes = detectMemoryBytes();
    run.environment.acPower = detectAcPower();
    run.environment.cpuClockResolutionMs = detectCpuClockResolutionMs();
    run.environment.isaCapabilities = detectIsaCapabilities();
    run.environment.gpuModel = "not-used";
    run.environment.driverVersion = "not-used";
    run.environment.powerProfile = options.powerProfile;
    run.environment.otherwiseIdle = options.otherwiseIdle;
    run.environment.notes = options.notes;

    // What the machine was permitted and configured to do, read rather than asserted. Static facts
    // now; the sampled ones bracket the scenarios below, because a clock that drooped or an indexer
    // that woke up matters over the region that was measured and not before it.
    const HostPolicy policy = readHostPolicy();
    run.environment.affinityCores = policy.affinityCores;
    run.environment.affinityMask = policy.affinityMask;
    run.environment.powerPlan = policy.powerPlan;
    adapter.describeEnvironment(run.environment);

    HostActivitySampler activity;
    activity.start();

    for (const ScenarioSpec& scenario : scenarios)
    {
        ResultRecord record;
        record.scenarioId = scenario.id;
        record.operationVersion = scenario.operationVersion;
        record.scenarioDigest = Sha256::of(json::toJson(scenario, false));
        record.correctnessClass = scenario.correctnessClass;
        record.tolerance = scenario.tolerance;
        record.bytesProcessed = scenario.bytesProcessed;
        record.elementsProcessed = scenario.elementsProcessed;

        const std::vector<std::string> scenarioProblems = scenario.validate();
        if (!scenarioProblems.empty())
        {
            // A malformed scenario is skipped with the reason rather than run. Running it would
            // measure whatever the adapter made of an invalid description, which is not a number
            // anyone can act on.
            record.skipped = true;
            record.skipReason = "scenario is not valid: " + scenarioProblems.front();
        }
        else if (options.dryRun)
        {
            record.skipped = true;
            record.skipReason = "dry run: the protocol is exercised and no timing is measured";
        }
        else
        {
            std::string skipReason;
            const bool ran = adapter.run(scenario, options, record, skipReason);
            if (options.forcedBackend.has_value())
            {
                record.requestedBackend = *options.forcedBackend;
            }
            if (!ran)
            {
                record.skipped = true;
                record.skipReason = skipReason.empty() ? "the engine reported no reason" : skipReason;
            }
            else if (options.forcedBackend.has_value())
            {
                if (record.actualBackend != *options.forcedBackend)
                {
                    record.skipped = true;
                    record.skipReason = "forced backend " + std::string(toString(*options.forcedBackend)) +
                                        " is not implemented by this scenario (it ran " +
                                        std::string(toString(record.actualBackend)) + ")";
                    record.timings = {};
                    record.outputDigest.clear();
                }
            }
        }

        run.results.push_back(std::move(record));
    }

    activity.stop();
    run.environment.cpuFrequencyMhz = activity.frequencyMhz();
    run.environment.backgroundLoad = activity.backgroundLoad();

    return run;
}

int runBenchmarkMain(int argc, char** argv, EngineAdapter& adapter)
{
    const std::string executable = argc > 0 ? argv[0] : "runner";

    RunOptions options;
    std::string error;
    if (!parseRunOptions(argc, argv, options, error))
    {
        std::cerr << error << '\n' << runnerUsage(executable);
        return 3;
    }
    resolveRunOptions(options);

    if (options.verifyCorpusOnly)
    {
        return verifyCorpusAndReport(options);
    }

    // Before the scenario list, because an adapter may decide what it can offer based on what
    // the options gave it.
    std::string prepareError;
    if (!adapter.prepare(options, prepareError))
    {
        std::cerr << prepareError << '\n';
        return 3;
    }

    std::vector<ScenarioSpec> scenarios = adapter.scenarios();
    if (!options.scenarioPath.empty())
    {
        scenarios.clear();
        if (const auto parsed = json::readScenariosFile(options.scenarioPath, scenarios); !parsed)
        {
            for (const std::string& message : parsed.errors)
            {
                std::cerr << "scenarios: " << message << '\n';
            }
            return 3;
        }
    }

    if (!options.scenarioDumpPath.empty())
    {
        // Written before the run, so the catalogue survives a run that fails partway and a later
        // run can still be given the same contract.
        std::string dumpError;
        if (!json::writeScenariosFile(options.scenarioDumpPath, scenarios, dumpError))
        {
            std::cerr << dumpError << '\n';
            return 1;
        }
    }

    const RunManifest run = buildRun(options, scenarios, adapter);

    // Validation before writing, not after. A result file that fails its own schema is worse than
    // no file: it will be read months later by someone who assumes it was checked when written.
    const std::vector<std::string> problems = run.validate();
    if (!problems.empty())
    {
        for (const std::string& problem : problems)
        {
            std::cerr << "run manifest: " << problem << '\n';
        }
        if (run.runnerBuild.commit.empty())
        {
            std::cerr << "no commit was detected; pass --commit <sha> to identify this run\n";
        }
        return 1;
    }

    std::string writeError;
    if (!json::writeRunFile(options.outputPath, run, writeError))
    {
        std::cerr << writeError << '\n';
        return 1;
    }

    std::cout << "run written to " << options.outputPath << ": " << run.results.size() << " scenario(s)\n";
    return 0;
}

} // namespace an::bench
