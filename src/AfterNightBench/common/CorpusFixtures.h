// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - fixtures backed by the real-data corpus.
//
// The synthetic generators can produce every shape the engine supports, but they cannot produce
// the thing real data is kept for: headers written by real capture software, sensor noise that no
// generator models, dimensions nobody would have chosen, and representations that exist because a
// camera vendor chose them. Scenarios that need those read from the corpus instead.
//
// This layer still links no engine, so it cannot open a FITS or a raw file. It does not need to:
// the raw-dump exporter already decodes a real file once, with the engine that can read it, and
// writes the engine-neutral dump this source consumes. That indirection is what lets every run be
// fed byte-identical real data without the fixture path depending on a codec - and it means a decode
// bug cannot change a run's input behind its digest.
//
// A corpus dataset that is not on this machine is an ordinary outcome, not an error. The corpus is
// host-local by design, so `acquire` reports the dataset by name and the scenario is skipped.
// Substituting a different file would be far worse than measuring nothing.

#pragma once

#include "Fixture.h"
#include "Scenario.h"
#include "SyntheticFixtures.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace an::bench
{

/// Fixtures read from raw dumps of real capture data, resolved through a corpus manifest.
class CorpusFixtureSource final : public FixtureSource
{
public:
    /// Construct a source over `manifest`, resolving relative paths against `root`.
    ///
    /// `root` is normally `resolveCorpusRoot`'s answer. An empty root is legal and means no
    /// dataset can be resolved, which is the correct state on a machine that holds no corpus: every
    /// corpus-backed scenario then skips by name.
    CorpusFixtureSource(CorpusManifest manifest, std::string root);

    /// True when `sourceId` names an entry in this source's manifest.
    ///
    /// Naming an entry is not the same as being able to produce it: the dataset may be absent from
    /// this machine. This answers which source owns the id, not whether the fixture exists.
    [[nodiscard]] bool knows(const std::string& sourceId) const;

    /// Read the dataset `sourceId` names.
    ///
    /// `seed` is accepted for the interface and ignored: real data is not generated, so there is
    /// nothing for a seed to vary. A scenario that needs several distinct real frames names several
    /// datasets rather than reseeding one.
    ///
    /// `descriptor` is what the scenario expects the data to be. It is checked against what the
    /// dump actually contains rather than trusted, because a scenario whose declared input does not
    /// match its real input reports a shape it did not measure. The fixture carries the dump's
    /// actual descriptor.
    ///
    /// Returns false and sets `error` when the id is unknown, the dataset is absent or fails its
    /// recorded size and digest, the dump cannot be read, or the dump contradicts either the
    /// manifest entry's expected descriptor or the scenario's.
    bool acquire(const std::string& sourceId, std::uint64_t seed, const SampleDescriptor& descriptor, Fixture& out,
                 std::string& error) override;

    /// Resolve the multi-frame dataset `sourceId` names, reading no pixels.
    ///
    /// Every frame the manifest lists is checked for presence and expected size. A set with any
    /// frame missing or wrong-sized is refused whole and names the first frame that failed: a
    /// pipeline measured over part of a calibration set is a different measurement, not a smaller
    /// one, and the number it produces would be attributed to the whole set.
    bool acquireSet(const std::string& sourceId, const SampleDescriptor& descriptor, FixtureSet& out,
                    std::string& error) override;

    /// Read and verify one frame of a resolved set.
    ///
    /// The frame's descriptor file is checked against its recorded digest and the raw dump verifies
    /// its own payload, so both halves of a frame are proven before its bytes reach a scenario.
    /// Deliberately per frame and per read: a set is far too large to verify in bulk up front, and
    /// verifying at the moment of use also catches a file replaced after resolution.
    bool loadFrame(const FixtureFrame& frame, Fixture& out, std::string& error) override;

    /// The root this source resolves against; empty when none is configured.
    [[nodiscard]] const std::string& root() const
    {
        return m_root;
    }

private:
    CorpusManifest m_manifest;
    std::string m_root;
};

/// What walking a corpus manifest found.
struct CorpusVerification
{
    std::uint32_t datasetsPresent = 0;
    std::uint32_t datasetsAbsent = 0;
    std::uint32_t setsPresent = 0;
    std::uint32_t setsAbsent = 0;
    std::uint64_t framesResolved = 0;
    std::uint64_t bytesResolved = 0;

    /// One line per dataset or set that could not be resolved, naming what was wrong.
    std::vector<std::string> problems;

    /// True when nothing in the manifest failed. An absent dataset is a problem here even though
    /// it is a normal outcome for a scenario: this is the check an operator runs to find out
    /// whether the corpus is whole, so "some of it is missing" is the answer it exists to give.
    bool ok = true;
};

/// Resolve every dataset and frame set in `manifest` through `source`, without running anything.
///
/// Reads no pixels. Single-file datasets are checked for presence, size and digest, since that is
/// what resolving one already does; frame sets are checked for presence and size only, because
/// digesting a 20 GB set is the cost the run itself pays as it reads each frame.
///
/// This is the check to run before a long measurement, and after writing a manifest by hand or by
/// script: it is the difference between finding a missing frame now and finding it forty minutes
/// into a benchmark.
[[nodiscard]] CorpusVerification verifyCorpus(const CorpusManifest& manifest, FixtureSource& source);

/// Routes each scenario's fixture request to whichever source owns its id.
///
/// The two sources have disjoint, self-declaring id spaces - the synthetic generators answer
/// `knows` for their own ids, the corpus source for the entries in its manifest - so routing needs
/// no extra field on the scenario and cannot silently prefer one over the other. An id neither
/// source claims is an error naming both, rather than a synthetic fallback: quietly generating
/// something when a scenario asked for real data would make the result describe the wrong input.
class CompositeFixtureSource final : public FixtureSource
{
public:
    explicit CompositeFixtureSource(std::uint32_t generatorVersion = kSyntheticGeneratorVersion);

    /// Attach a corpus. Without one, only synthetic ids resolve and every corpus id is unknown.
    void useCorpus(CorpusManifest manifest, std::string root);

    /// True once a corpus manifest has been attached.
    [[nodiscard]] bool hasCorpus() const
    {
        return m_corpus != nullptr;
    }

    bool acquire(const std::string& sourceId, std::uint64_t seed, const SampleDescriptor& descriptor, Fixture& out,
                 std::string& error) override;

    /// Routed to the corpus, which is the only source that has multi-frame datasets: the synthetic
    /// generators could produce one, but a pipeline measured on generated frames would prove the
    /// pipeline runs rather than what it costs on data anybody has.
    bool acquireSet(const std::string& sourceId, const SampleDescriptor& descriptor, FixtureSet& out,
                    std::string& error) override;

    bool loadFrame(const FixtureFrame& frame, Fixture& out, std::string& error) override;

private:
    SyntheticFixtureSource m_synthetic;
    std::unique_ptr<CorpusFixtureSource> m_corpus;
};

} // namespace an::bench
