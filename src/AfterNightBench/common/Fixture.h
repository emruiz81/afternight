// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the fixture API.
//
// The interface is separated from the deterministic synthetic generators and the corpus-backed
// source that implement it, so the runner can be written against a stable seam before the
// generators exist.
//
// A fixture hands the runner raw bytes plus the descriptor that explains them. It never hands over
// an engine image, because this layer links no engine.

#pragma once

#include "Descriptors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// One prepared benchmark input.
struct Fixture
{
    SampleDescriptor descriptor;
    std::vector<std::uint8_t> bytes;

    /// SHA-256 over `bytes`, recorded in the result so two runs can be shown to have been fed
    /// identical input rather than assumed to.
    std::string digest;

    /// Generator or dataset identity, plus the version of the generator that produced it.
    /// Fixture generation is versioned so that a generator change invalidates old digests loudly
    /// instead of quietly producing different pixels under the same name.
    std::string sourceId;
    std::uint32_t generatorVersion = 0;

    [[nodiscard]] bool empty() const
    {
        return bytes.empty();
    }
};

/// One frame of a multi-frame dataset, resolved but not read.
///
/// The frame's recorded size and digest travel with it so whoever reads it can verify it at that
/// moment, rather than the set having been verified in bulk long before the bytes were used.
struct FixtureFrame
{
    std::string id; ///< "<set>/<role>/<index>", stable and unique within a run.
    std::string absolutePath;
    std::string sha256;
    std::uint64_t sizeBytes = 0;
};

/// A prepared multi-frame input: what to read and in what order, not the bytes.
///
/// Deliberately not a vector of `Fixture`. The reference dataset is 232 frames and about 21.7 GB;
/// materialising it would exceed anything worth allocating and would put the loader's own footprint
/// into the pipeline's peak-memory figure. A
/// pipeline consumes frames the way the product does - one at a time - so the set hands over
/// handles and the consumer reads them as it needs them.
struct FixtureSet
{
    std::string sourceId;

    /// Identity over every member's recorded digest, in order, taken from the manifest.
    ///
    /// Recorded in the result the way a single fixture's digest is, so two runs can be shown to have been
    /// fed the same data - without the runner having hashed 21.7 GB to say so.
    std::string digest;

    /// What every frame in the set is expected to decode to.
    SampleDescriptor descriptor;

    struct Role
    {
        std::string name;
        std::vector<FixtureFrame> frames;
    };
    std::vector<Role> roles;

    [[nodiscard]] const Role* role(const std::string& name) const;
    [[nodiscard]] std::size_t frameCount() const;
    [[nodiscard]] bool empty() const
    {
        return roles.empty();
    }
};

/// Produces fixtures for scenarios.
class FixtureSource
{
public:
    virtual ~FixtureSource() = default;

    /// Build the fixture named by `sourceId` with `seed` into the shape `descriptor` asks for.
    ///
    /// Returns false and sets `error` when the source is unknown, or when a corpus dataset is not
    /// present on this machine. The caller reports that as a skipped scenario with the reason
    /// attached; it must never fall back to a different dataset.
    virtual bool acquire(const std::string& sourceId, std::uint64_t seed, const SampleDescriptor& descriptor,
                         Fixture& out, std::string& error) = 0;

    /// Resolve the multi-frame dataset named by `sourceId`.
    ///
    /// Resolution reads no pixels: it checks that every frame the manifest names is present and
    /// reports where each one lives. A set with any frame missing is refused whole rather than
    /// resolved short, because half a calibration set is not a smaller measurement but a different
    /// one. The default answers that this source has no sets.
    virtual bool acquireSet(const std::string& sourceId, const SampleDescriptor& descriptor, FixtureSet& out,
                            std::string& error);

    /// Read one frame of a resolved set, verifying it against its recorded size and digest.
    ///
    /// Verification happens here rather than at resolution time so the cost is paid per frame as
    /// the frame is used, and so a file replaced between resolution and use is still caught. The
    /// default answers that this source has no frames to read.
    virtual bool loadFrame(const FixtureFrame& frame, Fixture& out, std::string& error);
};

} // namespace an::bench
