// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The driver a fuzz target gets when the build has no libFuzzer: replay the seeds, then mutate them.
//
// It is not a coverage-guided fuzzer and does not pretend to be one. What it is, is the half of the
// campaign that runs everywhere the engine builds - including the MSVC host, where libFuzzer is not
// available - and the half that is *reproducible from two numbers*. Every input the sweep generates
// is a pure function of the seed and the iteration index, so a failure reported as iteration 91,204
// is re-run with `--seed <same> --start 91204 --iterations 1` and nothing else. A coverage-guided
// run reaches deeper; this one reaches the same place twice.
//
// Determinism costs one design decision: nothing the sweep discovers joins the corpus. A fuzzer that
// grew its corpus would make iteration 91,204 mean something different on the next run, and the
// reproducer would be a file rather than a number. The corpus is the target's seeds plus whatever
// `--corpus` names, fixed before the first iteration.
//
// **A crash leaves a reproducer.** The input under evaluation is recorded before each call, and the
// handlers for an abort, a fault and a watchdog timeout all write it out. That matters most for the
// failure a return code cannot express: an input that never comes back.

#include "FuzzHarness.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#include <windows.h>
#endif

namespace
{

using Bytes = std::vector<std::uint8_t>;

/// Whether an artifact has already been written for this process, so two handlers firing in turn do
/// not write two files and neither blocks waiting for the other.
std::atomic<bool> g_artifactWritten{false};

/// When the input being evaluated must have come back by, as a steady-clock count, or zero while no
/// input is being evaluated.
std::atomic<std::uint64_t> g_deadlineTicks{0u};

/// Whether the watchdog should keep running.
std::atomic<bool> g_watchdogRunning{true};

/// Writes the reproducer once, whichever handler got here first.
///
/// @param reason what stopped the process, as one word for the file name.
void writeArtifactOnce(const char* reason)
{
    bool expected = false;
    if (g_artifactWritten.compare_exchange_strong(expected, true))
    {
        an::fuzz::writeCurrentInputArtifact(reason);
    }
}

/// What a fault does: name it, write the input that caused it, and let the process die of it.
///
/// @param signalNumber the signal that was raised.
extern "C" void onFatalSignal(int signalNumber)
{
    const char* reason = signalNumber == SIGSEGV   ? "segv"
                         : signalNumber == SIGILL  ? "illegal"
                         : signalNumber == SIGFPE  ? "fpe"
                         : signalNumber == SIGABRT ? "abort"
                                                   : "signal";
    std::fprintf(stderr, "\n[fuzz] caught signal %d\n", signalNumber);
    writeArtifactOnce(reason);
    std::fflush(stderr);
    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
}

#ifdef _WIN32
/// What a structured exception does, which on Windows is what an access violation is.
///
/// The CRT's `signal` does not see one, so the reproducer would be lost without this.
///
/// @param exception what the operating system reported.
/// @return that the search continues, so the process still dies of it.
LONG WINAPI onStructuredException(EXCEPTION_POINTERS* exception)
{
    const DWORD code =
        exception != nullptr && exception->ExceptionRecord != nullptr ? exception->ExceptionRecord->ExceptionCode : 0u;
    std::fprintf(stderr, "\n[fuzz] caught structured exception 0x%08lx\n", static_cast<unsigned long>(code));
    writeArtifactOnce("exception");
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

/// Installs everything that turns a dying process into a written reproducer.
void installHandlers()
{
    std::signal(SIGSEGV, onFatalSignal);
    std::signal(SIGABRT, onFatalSignal);
    std::signal(SIGILL, onFatalSignal);
    std::signal(SIGFPE, onFatalSignal);
#ifdef _WIN32
    // A fuzzing run is not interactive. Without these, a failed assertion or an abort opens a dialog
    // and the campaign waits for a person who is not there.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    SetUnhandledExceptionFilter(onStructuredException);
#endif
}

/// A steady-clock reading as a plain count, so a deadline fits in one atomic.
///
/// @return milliseconds since the clock's epoch.
[[nodiscard]] std::uint64_t nowTicks()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// Ends the process when an input does not come back.
///
/// A hang is the one failure a fuzz target cannot report for itself, and the one most likely to be a
/// real defect rather than a slow input: an unbounded loop driven by a length field is exactly what a
/// mutated header produces.
///
/// @param pollMilliseconds how often to look.
void watchdog(std::uint32_t pollMilliseconds)
{
    while (g_watchdogRunning.load(std::memory_order_relaxed))
    {
        const std::uint64_t deadline = g_deadlineTicks.load(std::memory_order_relaxed);
        if (deadline != 0u && nowTicks() > deadline)
        {
            std::fprintf(stderr, "\n[fuzz] an input did not come back within its timeout\n");
            writeArtifactOnce("timeout");
            std::fflush(stderr);
            std::abort();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMilliseconds));
    }
}

/// A small, fast, reproducible generator.
///
/// SplitMix64, because the property that matters here is that a state derived from one integer is
/// already well mixed: every iteration builds its own generator from the seed and the iteration
/// index, so iteration 91,204 is the same input whether or not iteration 91,203 ran.
class Random
{
public:
    /// @param state the value to start from.
    explicit Random(std::uint64_t state) noexcept : m_state(state) {}

    /// @return the next value.
    [[nodiscard]] std::uint64_t next() noexcept
    {
        m_state += 0x9E3779B97F4A7C15ull;
        std::uint64_t value = m_state;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    /// @param bound one past the largest value wanted. Zero yields zero.
    /// @return a value below the bound.
    [[nodiscard]] std::size_t below(std::size_t bound) noexcept
    {
        return bound == 0u ? 0u : static_cast<std::size_t>(next() % bound);
    }

private:
    std::uint64_t m_state;
};

/// The values a length, an index or a count is most often wrong at.
///
/// A blind byte mutator reaches zero and reaches 255, and reaches 0x7FFFFFFF about once in four
/// billion. Writing them in by hand is what makes an off-by-one at a signed boundary a thing this
/// sweep finds in its first thousand iterations rather than never.
constexpr std::uint64_t kInterestingValues[] = {
    0u,
    1u,
    2u,
    7u,
    8u,
    15u,
    16u,
    31u,
    32u,
    63u,
    64u,
    127u,
    128u,
    255u,
    256u,
    32767u,
    32768u,
    65535u,
    65536u,
    2147483647u,
    2147483648u,
    4294967295u,
    4294967296u,
    9223372036854775807ull,
    9223372036854775808ull,
    18446744073709551615ull,
};

/// One input, built from one base and one generator.
///
/// @param base the corpus entry to start from.
/// @param corpus every entry, for the splice operator.
/// @param maxBytes the bound the target set.
/// @param random the generator, already positioned.
/// @return the mutated input.
[[nodiscard]] Bytes mutate(const Bytes& base, const std::vector<Bytes>& corpus, std::size_t maxBytes, Random& random)
{
    Bytes bytes = base;
    const std::size_t rounds = 1u + random.below(4u);
    for (std::size_t round = 0u; round < rounds; ++round)
    {
        switch (random.below(8u))
        {
        case 0: // Flip one bit.
            if (!bytes.empty())
            {
                const std::size_t index = random.below(bytes.size());
                bytes[index] = static_cast<std::uint8_t>(bytes[index] ^ (1u << random.below(8u)));
            }
            break;
        case 1: // Replace one byte.
            if (!bytes.empty())
            {
                bytes[random.below(bytes.size())] = static_cast<std::uint8_t>(random.next());
            }
            break;
        case 2: // Insert a run.
            if (bytes.size() < maxBytes)
            {
                const std::size_t at = random.below(bytes.size() + 1u);
                const std::size_t count = 1u + random.below(8u);
                const Bytes run(count, static_cast<std::uint8_t>(random.next()));
                const std::size_t room = maxBytes - bytes.size();
                bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), run.begin(),
                             run.begin() + static_cast<std::ptrdiff_t>(count < room ? count : room));
            }
            break;
        case 3: // Erase a run.
            if (!bytes.empty())
            {
                const std::size_t at = random.below(bytes.size());
                const std::size_t count = 1u + random.below(bytes.size() - at);
                bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                            bytes.begin() + static_cast<std::ptrdiff_t>(at + count));
            }
            break;
        case 4: // Splice a run of another entry over this one.
            if (!bytes.empty() && !corpus.empty())
            {
                const Bytes& other = corpus[random.below(corpus.size())];
                if (!other.empty())
                {
                    const std::size_t from = random.below(other.size());
                    const std::size_t to = random.below(bytes.size());
                    const std::size_t count = 1u + random.below(other.size() - from);
                    for (std::size_t index = 0u; index < count && to + index < bytes.size(); ++index)
                    {
                        bytes[to + index] = other[from + index];
                    }
                }
            }
            break;
        case 5: // Truncate.
            if (!bytes.empty())
            {
                bytes.resize(random.below(bytes.size()));
            }
            break;
        case 6: // Write an interesting value over a window.
            if (!bytes.empty())
            {
                const std::uint64_t value = kInterestingValues[random.below(std::size(kInterestingValues))];
                const std::size_t width = static_cast<std::size_t>(1u) << random.below(4u); // 1, 2, 4 or 8 bytes.
                const std::size_t at = random.below(bytes.size());
                const bool bigEndian = random.below(2u) != 0u;
                for (std::size_t index = 0u; index < width && at + index < bytes.size(); ++index)
                {
                    const std::size_t shift = bigEndian ? (width - 1u - index) : index;
                    bytes[at + index] = static_cast<std::uint8_t>((value >> (8u * shift)) & 0xFFu);
                }
            }
            break;
        default: // Append random bytes.
            if (bytes.size() < maxBytes)
            {
                const std::size_t room = maxBytes - bytes.size();
                const std::size_t count = 1u + random.below(room < 32u ? room : 32u);
                for (std::size_t index = 0u; index < count; ++index)
                {
                    bytes.push_back(static_cast<std::uint8_t>(random.next()));
                }
            }
            break;
        }
    }
    if (bytes.size() > maxBytes)
    {
        bytes.resize(maxBytes);
    }
    return bytes;
}

/// Reads a file whole.
///
/// @param path what to read.
/// @param bytes where to put it.
/// @return whether it was read.
[[nodiscard]] bool readFile(const std::filesystem::path& path, Bytes& bytes)
{
    std::FILE* file = an::fuzz::openFile(path.string().c_str(), "rb");
    if (file == nullptr)
    {
        return false;
    }
    bytes.clear();
    std::uint8_t buffer[4096];
    std::size_t read = 0u;
    while ((read = std::fread(buffer, 1u, sizeof(buffer), file)) != 0u)
    {
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    std::fclose(file);
    return true;
}

/// Writes a file whole.
///
/// @param path where to write.
/// @param bytes what to write.
/// @return whether it was written.
[[nodiscard]] bool writeFile(const std::filesystem::path& path, const Bytes& bytes)
{
    std::FILE* file = an::fuzz::openFile(path.string().c_str(), "wb");
    if (file == nullptr)
    {
        return false;
    }
    if (!bytes.empty())
    {
        std::fwrite(bytes.data(), 1u, bytes.size(), file);
    }
    std::fclose(file);
    return true;
}

/// Where reproducers go when nothing said.
///
/// The campaign's storage is test-owned rather than the production tree, which is the same rule
/// every suite in this repository follows; `AFTERNIGHT_TEST_STORAGE_ROOT` is how a runner states it.
///
/// @return the directory, which may not exist yet.
[[nodiscard]] std::filesystem::path defaultArtifactDirectory()
{
    const std::string root = an::fuzz::environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
    const std::filesystem::path base =
        root.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(root);
    return base / "fuzz-artifacts";
}

/// Everything the command line said.
struct Options
{
    std::uint64_t seed = 1u;
    std::uint64_t iterations = 0u; ///< Zero means the target's own default.
    std::uint64_t start = 0u;
    std::uint64_t maxSeconds = 0u; ///< Zero means no wall-clock bound.
    std::uint32_t timeoutMilliseconds = 20000u;
    std::string artifactDirectory;
    std::string corpusDirectory;
    std::string dumpCorpusDirectory;
    std::string replayPath;
    bool quiet = false;
};

/// What to do when the command line is wrong, and what the options mean.
void printUsage()
{
    std::fprintf(stderr,
                 "Usage: <target> [options]\n"
                 "  --seed N              the generator's seed; the sweep is a pure function of it (default 1)\n"
                 "  --iterations N        how many inputs to evaluate after the seeds (default 200000)\n"
                 "  --start N             begin the sweep at this iteration, to reproduce one input\n"
                 "  --max-seconds N       stop early after this much wall clock (default: no bound)\n"
                 "  --timeout-ms N        end the process when one input takes longer (default 20000)\n"
                 "  --artifacts DIR       where a reproducer is written\n"
                 "  --corpus DIR          extra seeds, read once before the sweep\n"
                 "  --dump-corpus DIR     write the target's own seeds there and exit\n"
                 "  --replay FILE         evaluate one input and exit\n"
                 "  --quiet               print the summary only\n");
}

/// Reads the command line.
///
/// @param argc how many arguments.
/// @param argv what they are.
/// @param options where to put them.
/// @return whether the line was understood.
[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        const bool hasNext = index + 1 < argc;
        const auto next = [&]() -> std::string { return hasNext ? std::string(argv[++index]) : std::string(); };
        if (argument == "--quiet")
        {
            options.quiet = true;
        }
        else if (!hasNext)
        {
            std::fprintf(stderr, "[fuzz] %s needs a value\n", argument.c_str());
            return false;
        }
        else if (argument == "--seed")
        {
            options.seed = std::strtoull(next().c_str(), nullptr, 10);
        }
        else if (argument == "--iterations")
        {
            options.iterations = std::strtoull(next().c_str(), nullptr, 10);
        }
        else if (argument == "--start")
        {
            options.start = std::strtoull(next().c_str(), nullptr, 10);
        }
        else if (argument == "--max-seconds")
        {
            options.maxSeconds = std::strtoull(next().c_str(), nullptr, 10);
        }
        else if (argument == "--timeout-ms")
        {
            options.timeoutMilliseconds = static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        }
        else if (argument == "--artifacts")
        {
            options.artifactDirectory = next();
        }
        else if (argument == "--corpus")
        {
            options.corpusDirectory = next();
        }
        else if (argument == "--dump-corpus")
        {
            options.dumpCorpusDirectory = next();
        }
        else if (argument == "--replay")
        {
            options.replayPath = next();
        }
        else
        {
            std::fprintf(stderr, "[fuzz] unknown option %s\n", argument.c_str());
            return false;
        }
    }
    return true;
}

/// Evaluates one input, under the watchdog and with the reproducer recorded.
///
/// @param bytes the input.
/// @param iteration which iteration this is.
/// @param timeoutMilliseconds how long it may take.
void evaluate(const Bytes& bytes, std::uint64_t iteration, std::uint32_t timeoutMilliseconds)
{
    an::fuzz::noteCurrentInput(bytes.data(), bytes.size(), iteration);
    g_deadlineTicks.store(timeoutMilliseconds == 0u ? 0u : nowTicks() + timeoutMilliseconds, std::memory_order_relaxed);
    (void)LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    g_deadlineTicks.store(0u, std::memory_order_relaxed);
}

} // namespace

/// Runs the target: seeds first, then the sweep.
///
/// @param argc how many arguments.
/// @param argv what they are.
/// @return zero when every input came back, two when the command line was wrong. A failure is a
///         crashing process rather than a return code, which is the only signal both drivers share.
int main(int argc, char** argv)
{
    Options options;
    if (!parseOptions(argc, argv, options))
    {
        printUsage();
        return 2;
    }

    const an::fuzz::TargetInfo info = an::fuzz::targetInfo();
    std::vector<Bytes> corpus = an::fuzz::seedCorpus();

    if (!options.dumpCorpusDirectory.empty())
    {
        std::error_code failure;
        std::filesystem::create_directories(options.dumpCorpusDirectory, failure);
        std::size_t written = 0u;
        for (std::size_t index = 0u; index < corpus.size(); ++index)
        {
            char name[64];
            std::snprintf(name, sizeof(name), "seed-%03zu.bin", index);
            if (writeFile(std::filesystem::path(options.dumpCorpusDirectory) / name, corpus[index]))
            {
                ++written;
            }
        }
        std::printf("[fuzz] %s: wrote %zu of %zu seeds to %s\n", info.name, written, corpus.size(),
                    options.dumpCorpusDirectory.c_str());
        return written == corpus.size() ? 0 : 1;
    }

    const std::filesystem::path artifacts = options.artifactDirectory.empty()
                                                ? defaultArtifactDirectory()
                                                : std::filesystem::path(options.artifactDirectory);
    std::error_code ignored;
    std::filesystem::create_directories(artifacts, ignored);
    an::fuzz::setArtifactDirectory(artifacts.string());

    installHandlers();
    std::thread watchdogThread;
    if (options.timeoutMilliseconds != 0u)
    {
        watchdogThread = std::thread(watchdog, 250u);
    }

    if (!options.replayPath.empty())
    {
        Bytes bytes;
        if (!readFile(options.replayPath, bytes))
        {
            std::fprintf(stderr, "[fuzz] could not read %s\n", options.replayPath.c_str());
            g_watchdogRunning.store(false, std::memory_order_relaxed);
            if (watchdogThread.joinable())
            {
                watchdogThread.join();
            }
            return 2;
        }
        evaluate(bytes, 0u, options.timeoutMilliseconds);
        std::printf("[fuzz] %s: replayed %s (%zu bytes) and it came back\n", info.name, options.replayPath.c_str(),
                    bytes.size());
        g_watchdogRunning.store(false, std::memory_order_relaxed);
        if (watchdogThread.joinable())
        {
            watchdogThread.join();
        }
        return 0;
    }

    if (!options.corpusDirectory.empty())
    {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(options.corpusDirectory, ignored))
        {
            Bytes bytes;
            if (entry.is_regular_file(ignored) && readFile(entry.path(), bytes))
            {
                corpus.push_back(std::move(bytes));
            }
        }
    }
    if (corpus.empty())
    {
        // A sweep with nothing to mutate would still run and still pass, which is the one outcome a
        // campaign must not be able to report.
        std::fprintf(stderr, "[fuzz] %s: the corpus is empty\n", info.name);
        g_watchdogRunning.store(false, std::memory_order_relaxed);
        if (watchdogThread.joinable())
        {
            watchdogThread.join();
        }
        return 1;
    }

    const std::uint64_t iterations = options.iterations != 0u ? options.iterations : 200000u;
    const auto started = std::chrono::steady_clock::now();

    std::uint64_t replayed = 0u;
    if (options.start == 0u)
    {
        for (const Bytes& seed : corpus)
        {
            evaluate(seed, replayed, options.timeoutMilliseconds);
            ++replayed;
        }
    }

    std::uint64_t evaluated = 0u;
    std::size_t largest = 0u;
    bool stoppedEarly = false;
    for (std::uint64_t index = 0u; index < iterations; ++index)
    {
        const std::uint64_t iteration = options.start + index;
        // The generator is built from the seed and the iteration, never carried between iterations,
        // which is what makes `--start` reproduce one input rather than a prefix of a run.
        Random random(options.seed * 0x9E3779B97F4A7C15ull + iteration + 1u);
        const Bytes& base = corpus[random.below(corpus.size())];
        const Bytes bytes = mutate(base, corpus, info.maxInputBytes, random);
        largest = bytes.size() > largest ? bytes.size() : largest;
        evaluate(bytes, iteration, options.timeoutMilliseconds);
        ++evaluated;

        if (options.maxSeconds != 0u && (index & 0x3FFu) == 0x3FFu)
        {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);
            if (static_cast<std::uint64_t>(elapsed.count()) >= options.maxSeconds)
            {
                stoppedEarly = true;
                break;
            }
        }
        if (!options.quiet && evaluated != 0u && (evaluated % 25000u) == 0u)
        {
            std::printf("[fuzz] %s: %llu inputs\n", info.name, static_cast<unsigned long long>(evaluated));
            std::fflush(stdout);
        }
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    std::printf("[fuzz] %s: seed %llu, %llu seeds replayed, %llu mutations evaluated from iteration %llu, "
                "largest input %zu bytes, %lld ms%s\n",
                info.name, static_cast<unsigned long long>(options.seed), static_cast<unsigned long long>(replayed),
                static_cast<unsigned long long>(evaluated), static_cast<unsigned long long>(options.start), largest,
                static_cast<long long>(elapsed.count()), stoppedEarly ? " (stopped on the wall-clock bound)" : "");
    // What the sweep reached, because the line above is the same line whether every input was
    // refused at the target's first branch or every path was exercised.
    char prefix[128];
    std::snprintf(prefix, sizeof(prefix), "[fuzz] %s:", info.name);
    an::fuzz::reportReached(prefix);
    std::fflush(stdout);

    g_watchdogRunning.store(false, std::memory_order_relaxed);
    if (watchdogThread.joinable())
    {
        watchdogThread.join();
    }
    return 0;
}
