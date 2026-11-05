// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "HostPolicy.h"

#include "ProcessClock.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <sched.h>
#include <unistd.h>
#endif

namespace an::bench
{
namespace
{

#if defined(_WIN32)

// The power-plan and per-processor clock queries live in powrprof.dll and nowhere else. They are
// resolved at run time rather than linked, so that reading them stays a property of this one file
// instead of a link dependency every project that touches the benchmark schema has to carry - and
// so a machine without the library reports the fields as unavailable rather than failing to start.
struct ProcessorPowerInformation
{
    ULONG number;
    ULONG maxMhz;
    ULONG currentMhz;
    ULONG mhzLimit;
    ULONG maxIdleState;
    ULONG currentIdleState;
};

constexpr int kProcessorInformation = 11; ///< POWER_INFORMATION_LEVEL::ProcessorInformation

using CallNtPowerInformationFn = LONG(WINAPI*)(int, PVOID, ULONG, PVOID, ULONG);
using PowerGetActiveSchemeFn = DWORD(WINAPI*)(HKEY, GUID**);
using PowerReadFriendlyNameFn = DWORD(WINAPI*)(HKEY, const GUID*, const GUID*, const GUID*, UCHAR*, DWORD*);

HMODULE powrprof()
{
    static HMODULE module = LoadLibraryW(L"powrprof.dll");
    return module;
}

template <typename FunctionT> FunctionT powrprofEntry(const char* name)
{
    HMODULE module = powrprof();
    return module == nullptr ? nullptr : reinterpret_cast<FunctionT>(GetProcAddress(module, name));
}

std::string narrow(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

#else

/// One line of a small sysfs or procfs file, without the trailing newline.
std::string readLine(const char* path)
{
    std::ifstream file(path);
    std::string line;
    if (file && std::getline(file, line))
    {
        return line;
    }
    return {};
}

#endif

std::string toHex(std::uint64_t value)
{
    if (value == 0)
    {
        return "0";
    }
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

/// Processor time the whole machine spent out of idle since boot, in milliseconds.
///
/// Zero when the platform declines to answer, which the caller reads as "no background figure"
/// rather than as "the machine was idle".
double systemBusyMilliseconds()
{
#if defined(_WIN32)
    FILETIME idle{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetSystemTimes(&idle, &kernel, &user) == 0)
    {
        return 0.0;
    }
    const auto ticks = [](const FILETIME& value) {
        ULARGE_INTEGER combined{};
        combined.LowPart = value.dwLowDateTime;
        combined.HighPart = value.dwHighDateTime;
        return static_cast<double>(combined.QuadPart);
    };
    // Windows counts idle time inside the kernel total, so busy is the total less the idle part.
    const double total = ticks(kernel) + ticks(user);
    return (total - ticks(idle)) / 10000.0;
#else
    std::ifstream stat("/proc/stat");
    std::string label;
    if (!stat || !(stat >> label) || label != "cpu")
    {
        return 0.0;
    }
    // user nice system idle iowait irq softirq steal, in clock ticks.
    double fields[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (double& field : fields)
    {
        if (!(stat >> field))
        {
            field = 0.0;
        }
    }
    const double total = fields[0] + fields[1] + fields[2] + fields[3] + fields[4] + fields[5] + fields[6] + fields[7];
    const double idle = fields[3] + fields[4];
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (ticksPerSecond <= 0)
    {
        return 0.0;
    }
    return (total - idle) * 1000.0 / static_cast<double>(ticksPerSecond);
#endif
}

} // namespace

HostPolicy readHostPolicy()
{
    HostPolicy policy;

#if defined(_WIN32)
    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) != 0)
    {
        policy.affinityCores = static_cast<std::uint32_t>(std::popcount(static_cast<std::uint64_t>(processMask)));
        policy.affinityMask = toHex(static_cast<std::uint64_t>(processMask));
    }

    if (auto getScheme = powrprofEntry<PowerGetActiveSchemeFn>("PowerGetActiveScheme"))
    {
        GUID* active = nullptr;
        if (getScheme(nullptr, &active) == ERROR_SUCCESS && active != nullptr)
        {
            if (auto readName = powrprofEntry<PowerReadFriendlyNameFn>("PowerReadFriendlyName"))
            {
                DWORD bytes = 0;
                if (readName(nullptr, active, nullptr, nullptr, nullptr, &bytes) == ERROR_SUCCESS && bytes > 0)
                {
                    std::vector<UCHAR> buffer(bytes, 0);
                    if (readName(nullptr, active, nullptr, nullptr, buffer.data(), &bytes) == ERROR_SUCCESS)
                    {
                        policy.powerPlan = narrow(reinterpret_cast<const wchar_t*>(buffer.data()));
                    }
                }
            }
            LocalFree(active);
        }
    }
#else
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0)
    {
        policy.affinityCores = static_cast<std::uint32_t>(CPU_COUNT(&allowed));
        std::uint64_t low = 0;
        for (int cpu = 0; cpu < 64 && cpu < CPU_SETSIZE; ++cpu)
        {
            if (CPU_ISSET(cpu, &allowed))
            {
                low |= std::uint64_t{1} << cpu;
            }
        }
        policy.affinityMask = toHex(low);
    }

    // Recorded, never changed: the governor and turbo state are root-owned, and pinning the profile
    // a run observed matters more than raising it.
    const std::string governor = readLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    const std::string preference = readLine("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference");
    const std::string noTurbo = readLine("/sys/devices/system/cpu/intel_pstate/no_turbo");
    std::ostringstream plan;
    if (!governor.empty())
    {
        plan << "governor=" << governor;
    }
    if (!preference.empty())
    {
        plan << (plan.tellp() > 0 ? " " : "") << "epp=" << preference;
    }
    if (!noTurbo.empty())
    {
        plan << (plan.tellp() > 0 ? " " : "") << "turbo=" << (noTurbo == "0" ? "on" : "off");
    }
    policy.powerPlan = plan.str();
#endif

    return policy;
}

double currentCpuMegahertz()
{
#if defined(_WIN32)
    auto query = powrprofEntry<CallNtPowerInformationFn>("CallNtPowerInformation");
    if (query == nullptr)
    {
        return 0.0;
    }
    const unsigned int processors = (std::max)(std::thread::hardware_concurrency(), 1u);
    std::vector<ProcessorPowerInformation> information(processors);
    const ULONG bytes = static_cast<ULONG>(information.size() * sizeof(ProcessorPowerInformation));
    if (query(kProcessorInformation, nullptr, 0, information.data(), bytes) != 0)
    {
        return 0.0;
    }
    double total = 0.0;
    for (const ProcessorPowerInformation& entry : information)
    {
        total += static_cast<double>(entry.currentMhz);
    }
    return total / static_cast<double>(information.size());
#else
    double total = 0.0;
    unsigned int counted = 0;
    for (unsigned int cpu = 0; cpu < std::thread::hardware_concurrency(); ++cpu)
    {
        const std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_cur_freq";
        std::ifstream file(path);
        double kilohertz = 0.0;
        if (file >> kilohertz)
        {
            total += kilohertz / 1000.0;
            ++counted;
        }
    }
    if (counted > 0)
    {
        return total / counted;
    }

    // Machines without cpufreq still report a clock per core in /proc/cpuinfo.
    std::ifstream info("/proc/cpuinfo");
    std::string line;
    while (std::getline(info, line))
    {
        const std::string::size_type colon = line.find(':');
        if (colon != std::string::npos && line.compare(0, 7, "cpu MHz") == 0)
        {
            total += std::strtod(line.c_str() + colon + 1, nullptr);
            ++counted;
        }
    }
    return counted > 0 ? total / counted : 0.0;
#endif
}

double backgroundLoadFraction(double systemBusyMs, double processCpuMs, double wallMs, std::uint32_t logicalCores)
{
    if (wallMs <= 0.0 || logicalCores == 0)
    {
        return 0.0;
    }
    const double capacityMs = wallMs * static_cast<double>(logicalCores);
    const double otherMs = systemBusyMs - processCpuMs;
    return (std::min)(1.0, (std::max)(0.0, otherMs / capacityMs));
}

HostActivitySampler::HostActivitySampler(std::chrono::milliseconds interval) : m_interval(interval)
{
    if (m_interval <= std::chrono::milliseconds::zero())
    {
        m_interval = std::chrono::milliseconds(1);
    }
}

HostActivitySampler::~HostActivitySampler()
{
    stop();
}

void HostActivitySampler::sampleFrequency()
{
    const double megahertz = currentCpuMegahertz();
    if (megahertz <= 0.0)
    {
        return;
    }
    const std::lock_guard<std::mutex> guard(m_mutex);
    if (m_frequency.samples == 0)
    {
        m_frequency.minimum = megahertz;
        m_frequency.maximum = megahertz;
        m_frequency.mean = megahertz;
        m_frequency.samples = 1;
        return;
    }
    m_frequency.minimum = (std::min)(m_frequency.minimum, megahertz);
    m_frequency.maximum = (std::max)(m_frequency.maximum, megahertz);
    const double previous = m_frequency.mean * static_cast<double>(m_frequency.samples);
    ++m_frequency.samples;
    m_frequency.mean = (previous + megahertz) / static_cast<double>(m_frequency.samples);
}

void HostActivitySampler::start()
{
    if (m_running.load())
    {
        return;
    }

    {
        const std::lock_guard<std::mutex> guard(m_mutex);
        m_frequency = SampledRange{};
        m_backgroundLoad = 0.0;
        m_systemBusyStartMs = systemBusyMilliseconds();
        m_processCpuStartMs = processCpuMilliseconds();
        m_startedAt = std::chrono::steady_clock::now();
    }

    // One reading before the worker starts, so a run shorter than the interval still has endpoints
    // rather than no clock at all.
    sampleFrequency();

    m_running.store(true);
    m_worker = std::thread([this] {
        while (m_running.load())
        {
            {
                std::unique_lock<std::mutex> lock(m_wakeMutex);
                if (m_wake.wait_for(lock, m_interval, [this] { return !m_running.load(); }))
                {
                    break;
                }
            }
            sampleFrequency();
        }
    });
}

void HostActivitySampler::stop()
{
    {
        // Under the wake lock, so the worker cannot miss the change between its check and its wait.
        const std::lock_guard<std::mutex> guard(m_wakeMutex);
        if (!m_running.exchange(false))
        {
            return;
        }
    }
    m_wake.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    sampleFrequency();

    const double busyMs = systemBusyMilliseconds();
    const double cpuMs = processCpuMilliseconds();
    const auto finished = std::chrono::steady_clock::now();

    const std::lock_guard<std::mutex> guard(m_mutex);
    const double wallMs = std::chrono::duration<double, std::milli>(finished - m_startedAt).count();
    m_backgroundLoad = backgroundLoadFraction(busyMs - m_systemBusyStartMs, cpuMs - m_processCpuStartMs, wallMs,
                                              (std::max)(std::thread::hardware_concurrency(), 1u));
}

SampledRange HostActivitySampler::frequencyMhz() const
{
    const std::lock_guard<std::mutex> guard(m_mutex);
    return m_frequency;
}

double HostActivitySampler::backgroundLoad() const
{
    const std::lock_guard<std::mutex> guard(m_mutex);
    return m_backgroundLoad;
}

} // namespace an::bench
