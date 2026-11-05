// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What a broken invariant does, and where the input that broke it is written.
//
// This is compiled into every fuzz target, including a libFuzzer build that has no driver of its
// own, because a target has to be able to say an invariant broke whatever is running it. Writing the
// input is the part that differs: the standalone driver names a directory and this writes a
// reproducer there, and libFuzzer names none, because it writes the input that crashed itself.

#include "FuzzHarness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace an::fuzz
{
namespace
{

/// Where a reproducer goes, and what is in the input being evaluated right now.
///
/// Plain file-scope objects rather than anything with a constructor, because a crash handler reads
/// them after the process has already lost whatever invariant it was relying on: a container here
/// would be one more thing that could be half-destroyed by the time it is read.
std::string g_artifactDirectory;
const std::uint8_t* g_currentData = nullptr;
std::size_t g_currentSize = 0u;
std::uint64_t g_currentIteration = 0u;

} // namespace

void setArtifactDirectory(std::string directory)
{
    g_artifactDirectory = std::move(directory);
}

namespace
{

/// The counters, as a fixed table rather than a map.
///
/// A fixed table means `countReached` allocates nothing on a path that runs once per input. Names are
/// compared by address first, because a target passes the same literal every time and that comparison
/// is one instruction. A target that names more than the table holds is stopped at the first name it
/// cannot count, because a counter dropped without a word is the silence the counters exist to break.
constexpr std::size_t kMaxCounters = 64u;
const char* g_counterNames[kMaxCounters] = {};
std::uint64_t g_counterValues[kMaxCounters] = {};
std::size_t g_counterCount = 0u;

} // namespace

void countReached(const char* what) noexcept
{
    for (std::size_t index = 0u; index < g_counterCount; ++index)
    {
        if (g_counterNames[index] == what || std::strcmp(g_counterNames[index], what) == 0)
        {
            ++g_counterValues[index];
            return;
        }
    }
    if (g_counterCount == kMaxCounters)
    {
        reportBrokenInvariant(__FILE__, __LINE__, "g_counterCount < kMaxCounters",
                              "a target names more reach counters than the table holds");
    }
    g_counterNames[g_counterCount] = what;
    g_counterValues[g_counterCount] = 1u;
    ++g_counterCount;
}

void reportReached(const char* prefix)
{
    if (g_counterCount == 0u)
    {
        std::printf("%s reached nothing it counts\n", prefix);
        return;
    }
    std::printf("%s reached", prefix);
    for (std::size_t index = 0u; index < g_counterCount; ++index)
    {
        std::printf("%s %s %llu", index == 0u ? "" : ",", g_counterNames[index],
                    static_cast<unsigned long long>(g_counterValues[index]));
    }
    std::printf("\n");
}

void noteCurrentInput(const std::uint8_t* data, std::size_t size, std::uint64_t iteration)
{
    g_currentData = data;
    g_currentSize = size;
    g_currentIteration = iteration;
}

void writeCurrentInputArtifact(const char* reason)
{
    if (g_artifactDirectory.empty())
    {
        return;
    }

    char name[256];
    std::snprintf(name, sizeof(name), "%s/%s-%s-%llu.bin", g_artifactDirectory.c_str(), targetInfo().name, reason,
                  static_cast<unsigned long long>(g_currentIteration));

    std::FILE* file = openFile(name, "wb");
    if (file == nullptr)
    {
        std::fprintf(stderr, "[fuzz] could not write the reproducer to %s\n", name);
        return;
    }
    if (g_currentSize != 0u && g_currentData != nullptr)
    {
        std::fwrite(g_currentData, 1u, g_currentSize, file);
    }
    std::fclose(file);
    std::fprintf(stderr, "[fuzz] reproducer written to %s (%zu bytes, iteration %llu)\n", name, g_currentSize,
                 static_cast<unsigned long long>(g_currentIteration));
}

void reportBrokenInvariant(const char* file, int line, const char* expression, const char* message)
{
    std::fprintf(stderr, "\n[fuzz] %s: broken invariant at %s:%d\n[fuzz]   %s\n[fuzz]   %s\n", targetInfo().name, file,
                 line, expression, message);
    writeCurrentInputArtifact("invariant");
    std::fflush(stderr);
    // The one failure signal libFuzzer recognises, and the one the standalone driver's handlers
    // recognise too. Not an exception: a target that threw would be caught by whatever the engine
    // wraps its own calls in and the campaign would report a pass.
    std::abort();
}

} // namespace an::fuzz
