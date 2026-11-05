// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "Scenario.h"

#include "Digest.h"
#include "Environment.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>
#include <vector>

namespace an::bench
{
namespace
{

constexpr std::uint32_t kMaximumTimedBatchIterations = 1'000'000'000u;

bool classNeedsTolerance(CorrectnessClass value)
{
    switch (value)
    {
    case CorrectnessClass::MaxUlp:
    case CorrectnessClass::AbsoluteRelative:
    case CorrectnessClass::LsbDistribution:
    case CorrectnessClass::RmsPsnr:
    case CorrectnessClass::GeometricResidual:
    case CorrectnessClass::EstimatorTolerance:
        return true;
    default:
        return false;
    }
}

} // namespace

std::optional<std::uint32_t> timedBatchIterations(const ScenarioSpec& scenario) noexcept
{
    const auto at = scenario.parameters.find(kTimedBatchParameter);
    if (at == scenario.parameters.end())
    {
        return 1u;
    }

    std::uint32_t value = 0u;
    const char* const first = at->second.data();
    const char* const last = first + at->second.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last || value == 0u || value > kMaximumTimedBatchIterations)
    {
        return std::nullopt;
    }
    return value;
}

std::vector<std::string> ScenarioSpec::validate() const
{
    std::vector<std::string> problems;

    if (id.empty())
    {
        problems.push_back("scenario id is empty");
    }
    if (group.empty())
    {
        problems.push_back("scenario '" + id + "' has no group");
    }
    if (operationVersion == 0)
    {
        problems.push_back("scenario '" + id + "' has operationVersion 0");
    }
    if (!input.isValid())
    {
        problems.push_back("scenario '" + id + "' has an invalid input descriptor");
    }
    if (corpusId.empty())
    {
        problems.push_back("scenario '" + id + "' names no corpus or generator id");
    }
    const std::optional<std::uint32_t> timedBatch = timedBatchIterations(*this);
    if (!timedBatch.has_value())
    {
        problems.push_back("scenario '" + id + "' has an invalid timedBatch parameter");
    }
    else if (inputSource == InputSource::CorpusFrameSet && *timedBatch != 1u)
    {
        problems.push_back("scenario '" + id + "' cannot batch a corpus frame set");
    }

    if (classNeedsTolerance(correctnessClass) && !(tolerance > 0.0))
    {
        problems.push_back("scenario '" + id + "' uses correctness class " + std::string(toString(correctnessClass)) +
                           " but has no positive tolerance");
    }

    return problems;
}

std::vector<std::string> CorpusEntry::validate() const
{
    std::vector<std::string> problems;

    if (id.empty())
    {
        problems.push_back("corpus entry has an empty id");
    }
    if (relativePath.empty())
    {
        problems.push_back("corpus entry '" + id + "' has no path");
    }
    if (sha256.size() != 64)
    {
        problems.push_back("corpus entry '" + id + "' has no valid sha256");
    }
    if (sizeBytes == 0)
    {
        problems.push_back("corpus entry '" + id + "' has size 0");
    }
    if (redistribution != "committed" && redistribution != "downloadable" && redistribution != "local-only")
    {
        problems.push_back("corpus entry '" + id + "' has unknown redistribution '" + redistribution + "'");
    }
    if (!expected.isValid())
    {
        problems.push_back("corpus entry '" + id + "' has an invalid expected descriptor");
    }

    return problems;
}

std::vector<std::string> CorpusFrame::validate() const
{
    std::vector<std::string> problems;

    if (relativePath.empty())
    {
        problems.push_back("corpus frame has no path");
    }
    if (sha256.size() != 64)
    {
        problems.push_back("corpus frame '" + relativePath + "' has no valid sha256");
    }
    if (sizeBytes == 0)
    {
        problems.push_back("corpus frame '" + relativePath + "' has size 0");
    }

    return problems;
}

std::size_t CorpusFrameSet::frameCount() const
{
    std::size_t count = 0;
    for (const CorpusFrameRole& entry : roles)
    {
        count += entry.frames.size();
    }
    return count;
}

std::uint64_t CorpusFrameSet::totalBytes() const
{
    std::uint64_t total = 0;
    for (const CorpusFrameRole& entry : roles)
    {
        for (const CorpusFrame& frame : entry.frames)
        {
            total += frame.sizeBytes;
        }
    }
    return total;
}

const CorpusFrameRole* CorpusFrameSet::role(const std::string& name) const
{
    const auto at = std::find_if(roles.begin(), roles.end(), [&](const CorpusFrameRole& e) { return e.role == name; });
    return at == roles.end() ? nullptr : &*at;
}

std::string CorpusFrameSet::identityDigest() const
{
    // Over the role names and the members' recorded digests, in order, with separators that cannot
    // occur inside either. Without them, two different sets could produce the same concatenation.
    Sha256 digest;
    for (const CorpusFrameRole& entry : roles)
    {
        digest.update(entry.role.data(), entry.role.size());
        digest.update("\n", 1);
        for (const CorpusFrame& frame : entry.frames)
        {
            digest.update(frame.sha256.data(), frame.sha256.size());
            digest.update("\n", 1);
        }
        digest.update("\x1e", 1);
    }
    return digest.hex();
}

std::vector<std::string> CorpusFrameSet::validate() const
{
    std::vector<std::string> problems;

    if (id.empty())
    {
        problems.push_back("corpus frame set has an empty id");
    }
    if (roles.empty())
    {
        problems.push_back("corpus frame set '" + id + "' has no roles");
    }
    if (redistribution != "committed" && redistribution != "downloadable" && redistribution != "local-only")
    {
        problems.push_back("corpus frame set '" + id + "' has unknown redistribution '" + redistribution + "'");
    }

    std::set<std::string> seenRoles;
    std::set<std::string> seenPaths;
    for (const CorpusFrameRole& entry : roles)
    {
        if (entry.role.empty())
        {
            problems.push_back("corpus frame set '" + id + "' has a role with no name");
        }
        else if (!seenRoles.insert(entry.role).second)
        {
            problems.push_back("corpus frame set '" + id + "' repeats the role '" + entry.role + "'");
        }
        if (entry.frames.empty())
        {
            // A role that names no frames is the shape a partially built manifest takes, and it
            // would otherwise run as a pipeline stage with nothing to do rather than as an error.
            problems.push_back("corpus frame set '" + id + "' role '" + entry.role + "' has no frames");
        }
        for (const CorpusFrame& frame : entry.frames)
        {
            auto frameProblems = frame.validate();
            problems.insert(problems.end(), frameProblems.begin(), frameProblems.end());
            if (!frame.relativePath.empty() && !seenPaths.insert(frame.relativePath).second)
            {
                problems.push_back("corpus frame set '" + id + "' uses '" + frame.relativePath + "' twice");
            }
        }
    }

    return problems;
}

void CorpusManifest::addSet(CorpusFrameSet set)
{
    m_sets.push_back(std::move(set));
}

const CorpusFrameSet* CorpusManifest::findSet(const std::string& id) const
{
    const auto at = std::find_if(m_sets.begin(), m_sets.end(), [&](const CorpusFrameSet& s) { return s.id == id; });
    return at == m_sets.end() ? nullptr : &*at;
}

void CorpusManifest::add(CorpusEntry entry)
{
    m_entries.push_back(std::move(entry));
}

const CorpusEntry* CorpusManifest::find(const std::string& id) const
{
    const auto at = std::find_if(m_entries.begin(), m_entries.end(), [&](const CorpusEntry& e) { return e.id == id; });
    return at == m_entries.end() ? nullptr : &*at;
}

CorpusManifest::Resolution CorpusManifest::resolve(const std::string& id, const std::string& root) const
{
    Resolution resolution;

    const CorpusEntry* entry = find(id);
    if (entry == nullptr)
    {
        resolution.skipReason = "dataset '" + id + "' is not in the corpus manifest";
        return resolution;
    }

    if (root.empty())
    {
        resolution.skipReason = "dataset '" + id + "' needs a corpus root, none was given";
        return resolution;
    }

    std::error_code error;
    const std::filesystem::path path = std::filesystem::path(root) / entry->relativePath;
    if (!std::filesystem::exists(path, error) || error)
    {
        // Absent is a normal outcome: the real corpus is host-local. Name the dataset so a reader
        // of the report can tell exactly what was not covered.
        resolution.skipReason = "dataset '" + id + "' not present at " + path.string();
        return resolution;
    }

    resolution.available = true;
    resolution.absolutePath = path.string();
    return resolution;
}

CorpusManifest::Resolution CorpusManifest::resolveVerified(const std::string& id, const std::string& root) const
{
    Resolution resolution = resolve(id, root);
    if (!resolution.available)
    {
        return resolution;
    }

    const CorpusEntry* entry = find(id);
    if (entry == nullptr || entry->sha256.empty())
    {
        // Nothing recorded to check against. Presence is all this manifest claimed for it.
        return resolution;
    }

    std::ifstream file(resolution.absolutePath, std::ios::binary);
    if (!file)
    {
        resolution.available = false;
        resolution.skipReason = "dataset '" + id + "' is present at " + resolution.absolutePath + " but unreadable";
        return resolution;
    }

    Sha256 digest;
    std::vector<char> buffer(64 * 1024);
    std::uint64_t size = 0;
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
    {
        const std::streamsize read = file.gcount();
        digest.update(buffer.data(), static_cast<std::size_t>(read));
        size += static_cast<std::uint64_t>(read);
    }

    if (entry->sizeBytes != 0 && size != entry->sizeBytes)
    {
        resolution.available = false;
        resolution.skipReason = "dataset '" + id + "' is " + std::to_string(size) + " bytes, not the recorded " +
                                std::to_string(entry->sizeBytes);
        return resolution;
    }

    const std::string actual = digest.hex();
    if (actual != entry->sha256)
    {
        resolution.available = false;
        resolution.skipReason = "dataset '" + id + "' hashes to " + actual + ", not the recorded " + entry->sha256;
    }
    return resolution;
}

std::string resolveCorpusRoot(const std::string& explicitRoot)
{
    return explicitRoot.empty() ? environmentValue(kCorpusRootVariable) : explicitRoot;
}

std::vector<std::string> CorpusManifest::validate() const
{
    std::vector<std::string> problems;
    std::set<std::string> seen;

    for (const CorpusEntry& entry : m_entries)
    {
        auto entryProblems = entry.validate();
        problems.insert(problems.end(), entryProblems.begin(), entryProblems.end());

        if (!entry.id.empty() && !seen.insert(entry.id).second)
        {
            problems.push_back("duplicate corpus entry id '" + entry.id + "'");
        }
    }

    for (const CorpusFrameSet& set : m_sets)
    {
        auto setProblems = set.validate();
        problems.insert(problems.end(), setProblems.begin(), setProblems.end());

        // One id space with the single-file entries: a scenario names one id, and which source
        // answers for it must not depend on lookup order.
        if (!set.id.empty() && !seen.insert(set.id).second)
        {
            problems.push_back("duplicate corpus id '" + set.id + "'");
        }
    }

    return problems;
}

std::vector<TransferBandwidthScenario> transferBandwidthScenarios()
{
    return {
        // A plane copied somewhere else: the memory-bandwidth floor, and the number every other
        // row here is read against.
        TransferBandwidthScenario{"transfer.copy.f32", TransferOperation::Copy, SampleType::Float32, 1u, Layout::Planar,
                                  2u},
        // A plane written and not read, which is the other half of the floor: a write-only pass
        // costs a memory system something different from a read-write one.
        TransferBandwidthScenario{"transfer.fill.f32", TransferOperation::Fill, SampleType::Float32, 1u, Layout::Planar,
                                  1u},
        // The two whole-pixel repacks, on the representation and channel count a colour camera
        // actually produces. These are the rows a vector instruction set is expected to move,
        // because one load produces every channel where a strided copy reads the row once per one.
        TransferBandwidthScenario{"transfer.deinterleave.rgb_u16", TransferOperation::Deinterleave, SampleType::UInt16,
                                  3u, Layout::Interleaved, 2u},
        TransferBandwidthScenario{"transfer.interleave.rgb_u16", TransferOperation::Interleave, SampleType::UInt16, 3u,
                                  Layout::Planar, 2u},
        // A payload rewritten in the other byte order, in place: what a codec does to a frame whose
        // file order is not this host's, and the one row here that touches each sample once while
        // still reading it.
        TransferBandwidthScenario{"transfer.byteswap.u16", TransferOperation::ByteSwap, SampleType::UInt16, 1u,
                                  Layout::Planar, 2u},
        // One channel taken out of an interleaved colour frame, which is the strided read a codec
        // does to look at one filter on its own. The whole image is read to reach a third of it and
        // a third is written, so the work is four thirds of the frame rather than twice it.
        TransferBandwidthScenario{"transfer.extract_channel.rgb_u16", TransferOperation::ExtractChannel,
                                  SampleType::UInt16, 3u, Layout::Interleaved, 4u, 3u},
        // Three monochrome frames joined into one colour frame, which is what a colour composition
        // of separately registered filters does. The three planes of the planar fixture are its
        // three sources, so the whole image is read once and written once.
        TransferBandwidthScenario{"transfer.compose_channels.rgb_u16", TransferOperation::ComposeChannels,
                                  SampleType::UInt16, 3u, Layout::Planar, 2u},
    };
}

std::vector<ScenarioSpec> narrowbandCompositionScenarios()
{
    constexpr std::uint32_t kWidth = 512u;
    constexpr std::uint32_t kHeight = 512u;
    constexpr std::uint64_t kPixels = std::uint64_t{kWidth} * kHeight;

    const auto make = [](const char* id, std::uint32_t channels) {
        ScenarioSpec scenario;
        scenario.id = id;
        scenario.group = "color";
        scenario.operationVersion = 1u;
        scenario.inputSource = InputSource::Synthetic;
        scenario.input.width = kWidth;
        scenario.input.height = kHeight;
        scenario.input.channels = channels;
        scenario.input.sampleType = SampleType::Float32;
        scenario.input.layout = Layout::Planar;
        scenario.input.byteOrder = ByteOrder::Native;
        scenario.corpusId = "synthetic.gradient";
        scenario.seed = 20260822u;
        scenario.correctnessClass = CorrectnessClass::BitExact;
        scenario.tolerance = 0.0;
        scenario.allowedBackends = {BackendId::Unspecified};
        return scenario;
    };

    ScenarioSpec extraction = make("color.narrowband.extract_bayer.f32", 1u);
    extraction.input.cfaPattern = "RGGB";
    extraction.parameters["products"] = "Ha,Green,HaOIII";
    extraction.parameters["scalings"] = "Native,HaUp,OiiiDown";
    extraction.elementsProcessed = kPixels * 5u;
    extraction.bytesProcessed = kPixels * sizeof(float) * 11u;
    extraction.workUnit = "mosaic-pixels";
    // The extraction reads the CFA phase from the descriptor and resamples with a half-pixel clamped separable
    // Lanczos4 kernel; this RGGB row measures that resampling, and the direct suite holds every phase.

    ScenarioSpec palettes = make("color.narrowband.palette_grid.f32", 3u);
    palettes.parameters["channels"] = "R=Ha,G=OIII,B=SII";
    palettes.parameters["palettes"] = "all12";
    palettes.parameters["intensities"] = "1,1,1";
    palettes.elementsProcessed = kPixels * 12u;
    palettes.bytesProcessed = kPixels * sizeof(float) * 39u;
    palettes.workUnit = "palette-pixels";
    ScenarioSpec mapper = make("color.narrowband.mapper.f32", 3u);
    mapper.parameters["channels"] = "R=Ha,G=OIII,B=SII";
    mapper.parameters["hues"] = "0,120,240";
    mapper.parameters["saturation"] = "0.75";
    mapper.parameters["modification"] = "none";
    mapper.parameters["background"] = "off";
    mapper.parameters["outputStf"] = "off";
    mapper.elementsProcessed = kPixels * 3u;
    mapper.bytesProcessed = kPixels * sizeof(float) * 6u;
    mapper.workUnit = "layer-pixels";
    ScenarioSpec nbrgb = make("color.narrowband.nbrgb.f32", 3u);
    nbrgb.parameters["channels"] = "RGB";
    nbrgb.parameters["rgbBandwidthNm"] = "100";
    nbrgb.parameters["narrowbandBandwidthNm"] = "7,8.5,8.5";
    nbrgb.parameters["scale"] = "1.2";
    nbrgb.parameters["fit"] = "0,0.92";
    nbrgb.parameters["rescale"] = "false";
    nbrgb.elementsProcessed = kPixels * 3u;
    nbrgb.bytesProcessed = kPixels * sizeof(float) * 9u;
    nbrgb.workUnit = "channel-pixels";
    return {std::move(extraction), std::move(palettes), std::move(mapper), std::move(nbrgb)};
}

} // namespace an::bench
