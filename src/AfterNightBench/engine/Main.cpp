// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineBench - the benchmark runner linked to AfterNightEngine.
//
// One process links one engine and writes one JSON result file; what reads it links no engine.
//
// It also carries the raw-dump exporter, which turns any file this engine can decode into a
// descriptor plus raw sample bytes - the path by which real image data reaches something that has no
// codec of its own.
//
// Exit codes for the benchmark run come from the shared harness and are documented in
// RunnerHarness.h. The export mode answers 0 when the pair was written, 1 when nothing was, and 3 for
// a command line it does not understand.

#include "RawDumpExport.h"
#include "EngineAdapter.h"

#include <iostream>
#include <string>

namespace
{

int usage()
{
    std::cerr << an::bench::runnerUsage("AfterNightEngineBench")
              << "\n"
                 "   or: AfterNightEngineBench --export <source-image> --export-to <descriptor.json>\n";
    return 3;
}

/// Export one decodable file as a raw dump. Returns 0 on success, 1 on failure, 3 on bad arguments.
int runExport(int argc, char** argv)
{
    std::string source;
    std::string destination;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        const bool hasValue = index + 1 < argc;
        if (argument == "--export" && hasValue)
        {
            source = argv[++index];
        }
        else if (argument == "--export-to" && hasValue)
        {
            destination = argv[++index];
        }
        else
        {
            return usage();
        }
    }
    if (source.empty() || destination.empty())
    {
        return usage();
    }

    std::string error;
    if (!an::bench::engine::exportRawDump(source, destination, error))
    {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "exported " << source << " to " << destination << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    for (int index = 1; index < argc; ++index)
    {
        if (std::string(argv[index]) == "--export")
        {
            return runExport(argc, argv);
        }
    }

    an::bench::engine::AfterNightEngineAdapter adapter;
    return an::bench::runBenchmarkMain(argc, argv, adapter);
}
