// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The key a result derived from a document is cached under: everything the result depends on, and nothing it does
/// not.
///
/// An engine image's pixel and semantic generations are per-image counters, so two images written once are
/// indistinguishable by them, and a counter alone cannot tell a stale histogram from a current one. A derived result -
/// statistics, a presentation buffer, a preview, a star list - is therefore keyed by the consumer: the document it was
/// derived from, the identity and generations of the asset it read, the revisions of every other input it read, the
/// part of the image it covered, the parameters it ran with and the presentation policy it was made for. Replacing a
/// document's asset or restoring a snapshot makes an asset with a new identity, so a result derived from the old one
/// can never be revived by generations that happen to coincide.
///
/// The selection, the parameters and the presentation policy are the caller's canonical encodings - the same text for
/// the same meaning - and are compared exactly, never by a digest that could collide.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace an::ui
{

struct DocumentRevision;

/// What a derived result depends on.
struct DerivedResultKey
{
    /// The document the result was derived from.
    std::uint64_t documentIdentity = 0u;

    /// The revision of the asset it read: identity, pixel, semantic and validity generations.
    an::consumer::AssetRevision asset{};

    /// The revisions of every other input it read - a mask, a reference frame - in the order the operation names them.
    std::vector<an::consumer::AssetRevision> secondaryRevisions;

    /// The part of the image it covered - a region, a channel, a frame - in the caller's canonical encoding; empty
    /// for the whole image.
    std::string selection;

    /// The parameters it ran with, in the caller's canonical encoding.
    std::string parameters;

    /// The presentation policy it was made for, in the caller's canonical encoding; empty when it is not a
    /// presentation.
    std::string presentationPolicy;

    /// Field-wise equality: two keys are equal only when every input is.
    [[nodiscard]] friend bool operator==(const DerivedResultKey& left, const DerivedResultKey& right) = default;
};

/// A key for a result derived from a document's revision, with the other inputs stated.
///
/// @param source the revision the result read, as a commit ticket or a read lease captured it.
/// @param secondaryRevisions the revisions of the other inputs it read.
/// @param selection the part of the image it covered.
/// @param parameters the parameters it ran with.
/// @param presentationPolicy the presentation policy it was made for.
/// @return the key.
[[nodiscard]] AFTERNIGHTCONTROLS_API DerivedResultKey
derivedResultKey(const DocumentRevision& source, std::vector<an::consumer::AssetRevision> secondaryRevisions = {},
                 std::string selection = {}, std::string parameters = {}, std::string presentationPolicy = {});

/// Hashes a key for an unordered container. Equal keys hash equally.
struct DerivedResultKeyHash
{
    /// The hash.
    ///
    /// @param key the key.
    /// @return a hash of every field.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::size_t operator()(const DerivedResultKey& key) const noexcept;
};

} // namespace an::ui
