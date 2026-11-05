// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the image snapshot round trips.

#pragma once

#include "Scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// An undo history's step: an image's samples and metadata written to a file and read back.
inline constexpr const char* kSnapshotRoundTrip = "codec.snapshot_roundtrip.f32";

/// The same step over a sky's continuous samples, the content a history keeps of a real image.
inline constexpr const char* kSnapshotRoundTripSky = "codec.snapshot_roundtrip.sky.f32";

/// The rows of the image snapshot round trip, timed over the complete composition a history step runs.
///
/// One 2048 by 2048 Float32 frame of the synthetic star field, carrying a header of forty cards, a world coordinate
/// solution and a lineage, is written and read back: the engine's snapshot container stores the samples, every
/// metadata object and the artifact identity in one file published through a pending file beside it. It is written
/// without flushing, as a history's swap file is, and read back whole. The row is held bit for bit: the samples that
/// come back are the ones that went out. It claims no cold-control exemption: the work is the image's bytes.
///
/// The second row writes the synthetic sky instead: a background that is not quantized, with a normal-like spread,
/// as a real image's sky is. A host's real-time scanner reads a freshly written file at its first open, and what it
/// costs depends on what the file holds - the star field's quantized samples cost it little, a sky's continuous ones
/// a great deal - so this is the row that measures a history step against the content a history keeps.
///
/// Each repeats its round trip in one timed region the number of times `prepare_hard_floor_scenarios.py` chose on
/// `bench-windows` for its 150 ms floor at a safety factor of two.
///
/// @return the two rows, in a fixed order.
inline std::vector<ScenarioSpec> snapshotScenarios()
{
    ScenarioSpec row;
    row.id = kSnapshotRoundTrip;
    row.group = "codec";
    row.operationVersion = 1;
    row.inputSource = InputSource::Synthetic;
    row.input.width = 2048u;
    row.input.height = 2048u;
    row.input.channels = 1u;
    row.input.sampleType = SampleType::Float32;
    row.input.layout = Layout::Planar;
    row.input.byteOrder = ByteOrder::Little;
    row.corpusId = "synthetic.stars";
    row.seed = 20261006u;
    row.parameters["format"] = "SNAPSHOT";
    row.parameters["cards"] = "40";
    row.parameters[kTimedBatchParameter] = "26";
    row.correctnessClass = CorrectnessClass::BitExact;
    row.workUnit = "pixels";
    row.elementsProcessed = std::uint64_t{row.input.width} * row.input.height;
    row.bytesProcessed = row.input.totalBytes() * 2u;
    row.allowedBackends = {BackendId::Unspecified};

    ScenarioSpec sky = row;
    sky.id = kSnapshotRoundTripSky;
    sky.corpusId = "synthetic.statistical_sky";
    sky.parameters[kTimedBatchParameter] = "26";
    return {row, sky};
}

/// Whether a row is one of the snapshot round trips this header declares.
///
/// @param id the row's id.
/// @return true for either round trip.
inline bool isSnapshotScenario(const std::string& id)
{
    return id == kSnapshotRoundTrip || id == kSnapshotRoundTripSky;
}

/// Whether a row is a snapshot round trip as this header declares it.
///
/// @param scenario the row a runner was handed.
/// @return true when its id, content, input and parameters are the declared ones.
inline bool matchesSnapshotScenario(const ScenarioSpec& scenario)
{
    const auto format = scenario.parameters.find("format");
    const auto cards = scenario.parameters.find("cards");
    const bool content = scenario.id == kSnapshotRoundTrip
                             ? scenario.corpusId == "synthetic.stars"
                             : scenario.id == kSnapshotRoundTripSky && scenario.corpusId == "synthetic.statistical_sky";
    return content && scenario.input.width == 2048u && scenario.input.height == 2048u &&
           scenario.input.channels == 1u && scenario.input.sampleType == SampleType::Float32 &&
           scenario.input.layout == Layout::Planar && format != scenario.parameters.end() &&
           format->second == "SNAPSHOT" && cards != scenario.parameters.end() && cards->second == "40";
}

/// The keyword of the `index`-th of the row's forty header cards.
///
/// @param index which card, from zero.
/// @return an eight-character FITS keyword.
inline std::string snapshotCardKeyword(std::uint32_t index)
{
    std::string keyword = "BENCH" + std::to_string(100u + index);
    return keyword;
}

/// The value of the `index`-th header card, a quoted FITS string of a fixed length.
///
/// @param index which card, from zero.
/// @return the value.
inline std::string snapshotCardValue(std::uint32_t index)
{
    return "'value of card " + std::to_string(100u + index) + "'";
}

} // namespace an::bench
