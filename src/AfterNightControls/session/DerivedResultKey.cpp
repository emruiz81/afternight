// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The derived-result key's construction from a document's revision, and its hash.

#include "session/DerivedResultKey.h"

#include "session/ImageDocument.h"

#include <functional>
#include <string_view>
#include <utility>

namespace an::ui
{

namespace
{

/// Folds one value into a running hash, so that the order of the fields matters and equal prefixes of different keys
/// do not collide by construction.
[[nodiscard]] std::size_t combine(std::size_t seed, std::size_t value) noexcept
{
    std::uint64_t mixed = static_cast<std::uint64_t>(seed) ^
                          (static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ull +
                           (static_cast<std::uint64_t>(seed) << 6u) + (static_cast<std::uint64_t>(seed) >> 2u));
    mixed ^= mixed >> 33u;
    mixed *= 0xff51afd7ed558ccdull;
    mixed ^= mixed >> 33u;
    return static_cast<std::size_t>(mixed);
}

[[nodiscard]] std::size_t combineRevision(std::size_t seed, const an::consumer::AssetRevision& revision) noexcept
{
    seed = combine(seed, static_cast<std::size_t>(revision.assetIdentity));
    seed = combine(seed, static_cast<std::size_t>(revision.pixelGeneration));
    seed = combine(seed, static_cast<std::size_t>(revision.semanticGeneration));
    return combine(seed, static_cast<std::size_t>(revision.validityGeneration));
}

} // namespace

DerivedResultKey derivedResultKey(const DocumentRevision& source,
                                  std::vector<an::consumer::AssetRevision> secondaryRevisions, std::string selection,
                                  std::string parameters, std::string presentationPolicy)
{
    return DerivedResultKey{.documentIdentity = source.documentIdentity,
                            .asset = source.asset,
                            .secondaryRevisions = std::move(secondaryRevisions),
                            .selection = std::move(selection),
                            .parameters = std::move(parameters),
                            .presentationPolicy = std::move(presentationPolicy)};
}

std::size_t DerivedResultKeyHash::operator()(const DerivedResultKey& key) const noexcept
{
    const std::hash<std::string_view> text;
    std::size_t seed = combine(0u, static_cast<std::size_t>(key.documentIdentity));
    seed = combineRevision(seed, key.asset);
    seed = combine(seed, key.secondaryRevisions.size());
    for (const an::consumer::AssetRevision& revision : key.secondaryRevisions)
    {
        seed = combineRevision(seed, revision);
    }
    seed = combine(seed, text(key.selection));
    seed = combine(seed, text(key.parameters));
    return combine(seed, text(key.presentationPolicy));
}

} // namespace an::ui
