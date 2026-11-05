// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The product's comparison difference, as neutral cases: two images and what the comparison view makes of them.
//
// The comparison view shows where two images differ as a third image. A case states one comparison over two neutral
// inputs: whether the view asks for every channel's difference or for one grey channel. Its record is the reference
// difference, written in the first image's representation; the engine fixture executable computes the same
// difference with the engine's operation and compares with the record by its classification.
//
// A record's id is `<operation>/<input>`, the input being the first image, and every record lives in
// `difference.expectations`.

#include "FixtureExpectation.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// Which difference the comparison view asks for.
enum class DifferenceMeasure : std::uint8_t
{
    /// Every channel's absolute difference, in the same channel.
    Absolute,
    /// One grey channel: the mean of the channels' absolute differences.
    Grey
};

/// One comparison of two inputs.
struct DifferenceCase
{
    /// The operation key, `difference.<measure>`; the record's id is `<operation>/<input>`.
    std::string operation;
    /// The first image, which the record names as its input.
    std::string input;
    /// The second image.
    std::string second;
    /// What the product does, in words; the record states it.
    std::string text;
    /// The product source whose calls the case follows, as `<library path>:<line>`.
    std::string source;
    DifferenceMeasure measure = DifferenceMeasure::Absolute;
    /// How the engine's answer is held to the record.
    ExpectationClass classification = ExpectationClass::Exact;
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    /// Why the answers differ, for an accepted case.
    std::string acceptedReason;
    /// Why a tolerance is what it is.
    std::string note;

    [[nodiscard]] std::string id() const
    {
        return operation + "/" + input;
    }
};

/// The record file every difference record lives in.
inline constexpr const char* kDifferenceExpectationFile = "difference.expectations";

/// Every case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<DifferenceCase>& differenceCases();

/// The case of this id, or null.
[[nodiscard]] const DifferenceCase* differenceCase(std::string_view id);

} // namespace an::tests::fixtures
