// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - normalization of one captured light to another.

#pragma once

#include "Scenario.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The parameter naming the light a pair row normalizes; the row's own corpus id is the reference.
inline constexpr const char* kNormalizationTargetParameter = "targetCorpusId";

/// The reference light of every pair: the first registered light of the Carina red session.
inline constexpr const char* kNormalizationPairReference = "carina.asi294mm.red.light0.aligned.f32";
/// A light of the same session, whose footprint covers the reference's almost entirely.
inline constexpr const char* kNormalizationPairSession = "carina.asi294mm.red.light7.aligned.f32";
/// A light of the second session, rotated against the first, covering part of the reference's footprint.
inline constexpr const char* kNormalizationPairRotated = "carina.asi294mm.red.light40.aligned.f32";

/// The global normalization rows over pairs of independently captured lights.
///
/// The normalization rows the audit ran derive their target from the reference by a known transform, so
/// they cannot show what a second exposure does - its own noise, its own gradient, its own footprint. These
/// rows match one registered light to another: a light of the same session under each of the three
/// methods, and a light of the second session, which overlaps the reference only in part, under the
/// scaling method. Both frames state that an exact zero is no data. The estimator is the median and its
/// absolute deviation, whose definition is exact, so every normalized
/// sample can be held to it; the iterative k-sigma estimator stays on the transformed-capture rows.
///
/// @return the rows, in a fixed order.
inline std::vector<ScenarioSpec> normalizationPairScenarios()
{
    struct Pair
    {
        const char* id;
        const char* target;
        const char* method;
    };
    const std::array<Pair, 4u> pairs = {
        Pair{"normalization.global_pair.carina_session.additive.f32", kNormalizationPairSession, "Additive"},
        Pair{"normalization.global_pair.carina_session.multiplicative.f32", kNormalizationPairSession,
             "Multiplicative"},
        Pair{"normalization.global_pair.carina_session.additive_scaling.f32", kNormalizationPairSession,
             "AdditiveScaling"},
        Pair{"normalization.global_pair.carina_rotated.additive_scaling.f32", kNormalizationPairRotated,
             "AdditiveScaling"}};
    std::vector<ScenarioSpec> rows;
    for (const Pair& pair : pairs)
    {
        ScenarioSpec row;
        row.id = pair.id;
        row.group = "normalization";
        row.operationVersion = 1;
        row.inputSource = InputSource::Corpus;
        row.corpusId = kNormalizationPairReference;
        row.input.width = 8288u;
        row.input.height = 5644u;
        row.input.channels = 1u;
        row.input.sampleType = SampleType::Float32;
        row.input.layout = Layout::Planar;
        row.input.byteOrder = ByteOrder::Little;
        row.correctnessClass = CorrectnessClass::EstimatorTolerance;
        // The tier the transformed-capture rows hold this family to.
        row.tolerance = 1e-3;
        row.parameters["method"] = pair.method;
        row.parameters["estimator"] = "MedianAbsoluteDeviation";
        row.parameters["validity"] = "zero-is-no-data";
        row.parameters[kNormalizationTargetParameter] = pair.target;
        row.workUnit = "samples";
        row.elementsProcessed = std::uint64_t{row.input.width} * row.input.height;
        row.bytesProcessed = row.elementsProcessed * sizeof(float) * 2u;
        row.allowedBackends = {BackendId::Unspecified};
        rows.push_back(row);
    }
    return rows;
}

} // namespace an::bench
