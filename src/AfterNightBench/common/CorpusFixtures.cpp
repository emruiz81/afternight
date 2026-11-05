// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "CorpusFixtures.h"

#include "Digest.h"
#include "RawDump.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace an::bench
{
namespace
{

/// True when `expected` says nothing about the shape, so there is nothing to check against.
bool isUnspecified(const SampleDescriptor& expected)
{
    return expected.width == 0 && expected.height == 0;
}

std::string describe(const SampleDescriptor& descriptor)
{
    std::ostringstream out;
    out << descriptor.width << 'x' << descriptor.height << ' ' << toString(descriptor.sampleType) << ' '
        << descriptor.channels << "ch " << toString(descriptor.layout);
    return out.str();
}

/// Compare only the properties that decide what the bytes mean.
///
/// Strides are deliberately excluded: a dump is written canonically packed, and a scenario that
/// declares a padded stride is describing how it will lay the data out rather than what the file
/// contains.
bool sameShape(const SampleDescriptor& left, const SampleDescriptor& right)
{
    return left.width == right.width && left.height == right.height && left.channels == right.channels &&
           left.sampleType == right.sampleType && left.layout == right.layout;
}

} // namespace

CorpusFixtureSource::CorpusFixtureSource(CorpusManifest manifest, std::string root)
    : m_manifest(std::move(manifest)), m_root(std::move(root))
{
}

bool CorpusFixtureSource::knows(const std::string& sourceId) const
{
    return m_manifest.find(sourceId) != nullptr;
}

bool CorpusFixtureSource::acquire(const std::string& sourceId, std::uint64_t seed, const SampleDescriptor& descriptor,
                                  Fixture& out, std::string& error)
{
    (void)seed; // Real data is not generated; see the header.

    const CorpusEntry* entry = m_manifest.find(sourceId);
    if (entry == nullptr)
    {
        error = "no corpus entry named '" + sourceId + "'";
        return false;
    }

    const CorpusManifest::Resolution resolution = m_manifest.resolveVerified(sourceId, m_root);
    if (!resolution.available)
    {
        // The dataset is host-local and this machine does not have it, or the file it does have is
        // not the one the manifest describes. Either way the scenario skips by name.
        error = resolution.skipReason.empty() ? ("corpus dataset '" + sourceId + "' is not available")
                                              : resolution.skipReason;
        return false;
    }

    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string dumpError;
    if (!readRawDump(resolution.absolutePath, dump, payload, dumpError))
    {
        error = "corpus dataset '" + sourceId + "': " + dumpError;
        return false;
    }

    // The manifest states what the dataset is expected to decode to. Checking it here is what
    // turns that field from documentation into a guarantee: a manifest that has drifted from its
    // data fails loudly instead of feeding a scenario something other than it declared.
    if (!isUnspecified(entry->expected) && !sameShape(entry->expected, dump.descriptor))
    {
        error = "corpus dataset '" + sourceId + "' contains " + describe(dump.descriptor) +
                " but the manifest expects " + describe(entry->expected);
        return false;
    }

    if (!isUnspecified(descriptor) && !sameShape(descriptor, dump.descriptor))
    {
        error = "corpus dataset '" + sourceId + "' contains " + describe(dump.descriptor) +
                " but the scenario asks for " + describe(descriptor);
        return false;
    }

    out.descriptor = dump.descriptor;
    out.bytes = std::move(payload);
    // `readRawDump` has already verified the payload against this digest, so recomputing it here
    // would hash a hundred megabytes to reach the answer we were just given.
    out.digest = dump.payloadDigest;
    out.sourceId = sourceId;
    // Corpus fixtures carry no generator version: nothing generated them, and their identity is
    // the recorded digest rather than a version number that could be bumped.
    out.generatorVersion = 0;
    return true;
}

bool CorpusFixtureSource::acquireSet(const std::string& sourceId, const SampleDescriptor& descriptor, FixtureSet& out,
                                     std::string& error)
{
    const CorpusFrameSet* set = m_manifest.findSet(sourceId);
    if (set == nullptr)
    {
        error = "no corpus frame set named '" + sourceId + "'";
        return false;
    }

    if (m_root.empty())
    {
        error = "corpus frame set '" + sourceId + "' needs a corpus root and none is configured";
        return false;
    }

    if (!isUnspecified(set->expected) && !isUnspecified(descriptor) && !sameShape(set->expected, descriptor))
    {
        error = "corpus frame set '" + sourceId + "' holds " + describe(set->expected) + " but the scenario asks for " +
                describe(descriptor);
        return false;
    }

    FixtureSet resolved;
    resolved.sourceId = sourceId;
    resolved.descriptor = set->expected;
    // From the manifest, so the set has an identity without a byte of it being read. Its per-frame
    // digests are proven later, one at a time, as `loadFrame` reads them.
    resolved.digest = set->identityDigest();

    const std::filesystem::path root(m_root);
    for (const CorpusFrameRole& role : set->roles)
    {
        FixtureSet::Role entry;
        entry.name = role.role;
        for (std::size_t index = 0; index < role.frames.size(); ++index)
        {
            const CorpusFrame& frame = role.frames[index];
            const std::filesystem::path path = root / frame.relativePath;

            std::error_code status;
            if (!std::filesystem::is_regular_file(path, status))
            {
                error = "corpus frame set '" + sourceId + "' is incomplete: '" + frame.relativePath +
                        "' is not on this machine";
                return false;
            }
            const std::uintmax_t actual = std::filesystem::file_size(path, status);
            if (status || static_cast<std::uint64_t>(actual) != frame.sizeBytes)
            {
                error = "corpus frame set '" + sourceId + "': '" + frame.relativePath + "' is " +
                        std::to_string(static_cast<std::uint64_t>(actual)) + " bytes, the manifest records " +
                        std::to_string(frame.sizeBytes);
                return false;
            }

            FixtureFrame handle;
            handle.id = sourceId + "/" + role.role + "/" + std::to_string(index);
            handle.absolutePath = path.string();
            handle.sha256 = frame.sha256;
            handle.sizeBytes = frame.sizeBytes;
            entry.frames.push_back(std::move(handle));
        }
        resolved.roles.push_back(std::move(entry));
    }

    out = std::move(resolved);
    return true;
}

bool CorpusFixtureSource::loadFrame(const FixtureFrame& frame, Fixture& out, std::string& error)
{
    std::ifstream file(frame.absolutePath, std::ios::binary);
    if (!file)
    {
        error = "frame '" + frame.id + "': cannot open '" + frame.absolutePath + "'";
        return false;
    }

    Sha256 digest;
    std::vector<char> buffer(64 * 1024);
    std::uint64_t read = 0;
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
    {
        digest.update(buffer.data(), static_cast<std::size_t>(file.gcount()));
        read += static_cast<std::uint64_t>(file.gcount());
    }
    if (read != frame.sizeBytes)
    {
        error = "frame '" + frame.id + "' is " + std::to_string(read) + " bytes, the manifest records " +
                std::to_string(frame.sizeBytes);
        return false;
    }
    if (digest.hex() != frame.sha256)
    {
        // The substitution the recorded digest exists to catch: a file that kept its name and its
        // size but is not the one the manifest describes.
        error = "frame '" + frame.id + "' does not match its recorded digest";
        return false;
    }

    RawDump dump;
    std::vector<std::uint8_t> payload;
    std::string dumpError;
    if (!readRawDump(frame.absolutePath, dump, payload, dumpError))
    {
        error = "frame '" + frame.id + "': " + dumpError;
        return false;
    }

    out.descriptor = dump.descriptor;
    out.bytes = std::move(payload);
    out.digest = dump.payloadDigest;
    out.sourceId = frame.id;
    out.generatorVersion = 0;
    return true;
}

CompositeFixtureSource::CompositeFixtureSource(std::uint32_t generatorVersion) : m_synthetic(generatorVersion) {}

void CompositeFixtureSource::useCorpus(CorpusManifest manifest, std::string root)
{
    m_corpus = std::make_unique<CorpusFixtureSource>(std::move(manifest), std::move(root));
}

bool CompositeFixtureSource::acquire(const std::string& sourceId, std::uint64_t seed,
                                     const SampleDescriptor& descriptor, Fixture& out, std::string& error)
{
    if (SyntheticFixtureSource::knows(sourceId))
    {
        return m_synthetic.acquire(sourceId, seed, descriptor, out, error);
    }
    if (m_corpus && m_corpus->knows(sourceId))
    {
        return m_corpus->acquire(sourceId, seed, descriptor, out, error);
    }

    // Say which sources were consulted. "Unknown fixture" sends a reader looking for a typo when
    // the real cause is usually a corpus manifest that was not passed to the runner at all.
    error = "no fixture source claims '" + sourceId + "': it is not a synthetic generator" +
            (m_corpus ? " and not an entry in the corpus manifest" : " and no corpus manifest was loaded");
    return false;
}

bool CompositeFixtureSource::acquireSet(const std::string& sourceId, const SampleDescriptor& descriptor,
                                        FixtureSet& out, std::string& error)
{
    if (!m_corpus)
    {
        error = "multi-frame dataset '" + sourceId + "' was requested and no corpus manifest was loaded";
        return false;
    }
    return m_corpus->acquireSet(sourceId, descriptor, out, error);
}

bool CompositeFixtureSource::loadFrame(const FixtureFrame& frame, Fixture& out, std::string& error)
{
    if (!m_corpus)
    {
        error = "frame '" + frame.id + "' was requested and no corpus manifest was loaded";
        return false;
    }
    return m_corpus->loadFrame(frame, out, error);
}

CorpusVerification verifyCorpus(const CorpusManifest& manifest, FixtureSource& source)
{
    CorpusVerification report;

    for (const CorpusEntry& entry : manifest.entries())
    {
        Fixture fixture;
        std::string error;
        // Resolving a single-file dataset reads it, which is also what verifies it. These are the
        // small ones; the sets below are where reading everything would be the wrong trade.
        if (source.acquire(entry.id, 0, {}, fixture, error))
        {
            ++report.datasetsPresent;
            report.bytesResolved += fixture.bytes.size();
        }
        else
        {
            ++report.datasetsAbsent;
            report.problems.push_back(error);
            report.ok = false;
        }
    }

    for (const CorpusFrameSet& set : manifest.sets())
    {
        FixtureSet resolved;
        std::string error;
        if (source.acquireSet(set.id, {}, resolved, error))
        {
            ++report.setsPresent;
            report.framesResolved += resolved.frameCount();
            report.bytesResolved += set.totalBytes();
        }
        else
        {
            ++report.setsAbsent;
            report.problems.push_back(error);
            report.ok = false;
        }
    }

    return report;
}

} // namespace an::bench
