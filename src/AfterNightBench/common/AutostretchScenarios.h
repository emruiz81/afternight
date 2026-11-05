// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the complete autostretch rows.

#pragma once

#include "AutostretchOracle.h"
#include "Scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The parameter naming what an autostretch row's clock holds.
inline constexpr const char* kAutostretchBoundaryParameter = "timedBoundary";
/// The whole Apply: the image copied, its statistics, the transfer derived and applied.
inline constexpr const char* kAutostretchComplete = "complete";
/// The statistics alone, measured over the source the way the composition measures them.
inline constexpr const char* kAutostretchStatistics = "statistics";
/// The transfer derived from statistics measured before the clock and applied into a new image, as a preview
/// that measured its image once applies it again.
inline constexpr const char* kAutostretchPrepared = "prepared";

/// The parameter naming how a row's samples are made from its fixture before the clock.
inline constexpr const char* kAutostretchSamplesParameter = "samples";
/// Sixteen-bit codes carried into the unit interval, as the statistics rows do.
inline constexpr const char* kAutostretchCodes = "codes-over-65535";
/// A colour frame made from a Bayer capture: each two-by-two block one pixel, its red, the mean of its two
/// greens and its blue, each over the code range.
inline constexpr const char* kAutostretchBinnedColour = "binned-rggb-over-65535";
/// The fixture's own samples.
inline constexpr const char* kAutostretchAsGiven = "as-given";

/// The calibrated, registered light the continuous rows read, from the integration corpus.
inline constexpr const char* kAutostretchCalibratedLight = "carina.asi294mm.red.light0.aligned.f32";

/// The automatic stretch's shadows clipping, in normalized median absolute deviations, every run states.
inline constexpr double kAutostretchShadowsClipping = -2.8;
/// The level the automatic stretch moves the background to, every run states.
inline constexpr double kAutostretchTargetBackground = 0.25;

/// The complete autostretch rows.
///
/// The product's Apply Stretch copies the shown image, measures every channel's median and median
/// absolute deviation, derives a midtones transfer from them - one shared by every channel when linked,
/// one per channel otherwise - and applies it to the copy. These rows time that composition with the
/// product's defaults over two mono captures and one colour frame made from a Bayer capture, linked and
/// unlinked, and over a calibrated light, whose continuous samples no sixteen-bit grid shortens the
/// exact median of, and time its two stages apart: the statistics alone, and the transfer applied from
/// statistics measured before the clock, which is what a preview or a histogram process that measured
/// its image once does every time it applies it again. A flat frame and a synthetic colour star field
/// reach the guarded deviation and the clipped highlights. Every row is held to
/// `AutostretchOracle.h`.
///
/// @return the rows, in a fixed order.
inline std::vector<ScenarioSpec> autostretchScenarios()
{
    struct Source
    {
        const char* name;
        const char* corpusId;
        std::uint32_t width;
        std::uint32_t height;
        const char* samples;
        std::uint32_t channels;
    };
    // The colour frame is made from the capture, so the row reads the capture's own descriptor. The calibrated
    // light is the Carina field calibrated and registered by the pipeline: continuous Float32, a few samples
    // below zero and above one, as an integrated image is.
    const Source sources[] = {
        {"eagle", "eagle.qhy533m.ha.light", 3008u, 3004u, kAutostretchCodes, 1u},
        {"carina", "carina.asi294mm.red.light", 8288u, 5644u, kAutostretchCodes, 1u},
        {"ngc253_rgb", "ngc253.qhy183c.light", 5544u, 3684u, kAutostretchBinnedColour, 3u},
        {"carina_calibrated", kAutostretchCalibratedLight, 8288u, 5644u, kAutostretchAsGiven, 1u}};
    std::vector<ScenarioSpec> rows;
    const auto declare = [&](const Source& source, const char* boundary, const char* linkage) {
        ScenarioSpec row;
        const bool colour = source.channels == 3u;
        const std::string stem = std::string("autostretch.") + boundary + "." + source.name;
        row.id = stem + (colour && std::string(boundary) != kAutostretchStatistics ? std::string(".") + linkage : "") +
                 ".f32";
        row.group = "autostretch";
        row.operationVersion = 1;
        row.inputSource = InputSource::Corpus;
        row.corpusId = source.corpusId;
        row.input.width = source.width;
        row.input.height = source.height;
        row.input.channels = 1u;
        row.input.sampleType =
            source.samples == std::string(kAutostretchAsGiven) ? SampleType::Float32 : SampleType::UInt16;
        row.input.layout = Layout::Planar;
        row.input.byteOrder = ByteOrder::Little;
        // A row that writes an image is held to its own transfer; one that measures only, to the exact
        // statistics within the tier a histogram median meets.
        const bool measures = std::string(boundary) == kAutostretchStatistics;
        row.correctnessClass = measures ? CorrectnessClass::EstimatorTolerance : CorrectnessClass::AbsoluteRelative;
        row.tolerance = measures ? kAutostretchStatisticsTolerance : 1e-6;
        row.parameters[kAutostretchBoundaryParameter] = boundary;
        row.parameters[kAutostretchSamplesParameter] = source.samples;
        row.parameters["linked"] = linkage == std::string("linked") ? "true" : "false";
        row.parameters["shadowsClipping"] = "-2.8";
        row.parameters["targetBackground"] = "0.25";
        const std::uint64_t pixels = colour ? std::uint64_t{source.width / 2u} * (source.height / 2u)
                                            : std::uint64_t{source.width} * source.height;
        row.workUnit = "samples";
        row.elementsProcessed = pixels * source.channels;
        row.bytesProcessed = row.elementsProcessed * sizeof(float);
        row.allowedBackends = {BackendId::Unspecified};
        rows.push_back(row);
    };
    for (const char* boundary : {kAutostretchComplete, kAutostretchStatistics, kAutostretchPrepared})
    {
        for (const Source& source : sources)
        {
            if (source.channels == 1u || std::string(boundary) == kAutostretchStatistics)
            {
                declare(source, boundary, "unlinked");
                continue;
            }
            declare(source, boundary, "unlinked");
            declare(source, boundary, "linked");
        }
    }

    ScenarioSpec flat;
    flat.id = "autostretch.complete.flat.f32";
    flat.group = "autostretch";
    flat.operationVersion = 1;
    flat.inputSource = InputSource::Synthetic;
    flat.corpusId = "synthetic.constant";
    flat.seed = 20261002u;
    flat.input.width = 1024u;
    flat.input.height = 1024u;
    flat.input.channels = 1u;
    flat.input.sampleType = SampleType::Float32;
    flat.input.layout = Layout::Planar;
    flat.input.byteOrder = ByteOrder::Little;
    flat.correctnessClass = CorrectnessClass::AbsoluteRelative;
    flat.tolerance = 1e-6;
    flat.parameters[kAutostretchBoundaryParameter] = kAutostretchComplete;
    flat.parameters[kAutostretchSamplesParameter] = kAutostretchAsGiven;
    flat.parameters["linked"] = "false";
    flat.parameters["shadowsClipping"] = "-2.8";
    flat.parameters["targetBackground"] = "0.25";
    flat.workUnit = "samples";
    flat.elementsProcessed = std::uint64_t{flat.input.width} * flat.input.height;
    flat.bytesProcessed = flat.elementsProcessed * sizeof(float);
    flat.allowedBackends = {BackendId::Unspecified};
    rows.push_back(flat);

    ScenarioSpec stars = flat;
    stars.id = "autostretch.complete.stars_rgb.linked.f32";
    stars.corpusId = "synthetic.stars";
    stars.input.width = 2048u;
    stars.input.height = 2048u;
    stars.input.channels = 3u;
    stars.parameters["linked"] = "true";
    stars.elementsProcessed = std::uint64_t{stars.input.width} * stars.input.height * 3u;
    stars.bytesProcessed = stars.elementsProcessed * sizeof(float);
    rows.push_back(stars);
    return rows;
}

} // namespace an::bench
