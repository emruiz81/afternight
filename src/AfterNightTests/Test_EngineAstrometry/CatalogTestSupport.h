// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace catalog_test
{

[[nodiscard]] inline an::EngineContext makeEngine(std::uint32_t concurrency = 8u,
                                                  std::uint64_t memoryLimit = 64u * 1024u * 1024u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = memoryLimit;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] inline an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 4u,
                                                        std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// One entry, spelled at the call site rather than built through eight setters.
[[nodiscard]] inline an::CatalogEntry makeEntry(std::string identifier, double rightAscensionDegrees,
                                                double declinationDegrees, std::optional<double> magnitude = {},
                                                std::string commonName = {}, std::string designation = {})
{
    an::CatalogEntry entry;
    entry.identifier = std::move(identifier);
    entry.commonName = std::move(commonName);
    entry.designation = std::move(designation);
    entry.coordinate =
        an::SkyCoordinate{.rightAscensionDegrees = rightAscensionDegrees, .declinationDegrees = declinationDegrees};
    entry.magnitude = magnitude;
    return entry;
}

/// A catalog built from entries, failing the test rather than the caller when the build refuses.
[[nodiscard]] inline an::SkyCatalog buildOrFail(const an::ExecutionContext& execution,
                                                std::vector<an::CatalogEntry> entries,
                                                const an::SkyCatalogRequest& request = {})
{
    an::Result<an::SkyCatalog> built = an::buildSkyCatalog(execution, std::move(entries), request);
    EXPECT_TRUE(built.hasValue()) << built.status().message();
    return built.hasValue() ? std::move(*built) : an::SkyCatalog{};
}

/// The identifiers a query answered with, in the order it answered.
[[nodiscard]] inline std::vector<std::string> identifiersOf(const an::SkyCatalog& catalog,
                                                            const std::vector<std::uint32_t>& indices)
{
    std::vector<std::string> names;
    names.reserve(indices.size());
    for (const std::uint32_t index : indices)
    {
        const an::CatalogEntry* const entry = catalog.entry(index);
        names.push_back(entry == nullptr ? std::string{"<out-of-range>"} : entry->identifier);
    }
    return names;
}

} // namespace catalog_test
