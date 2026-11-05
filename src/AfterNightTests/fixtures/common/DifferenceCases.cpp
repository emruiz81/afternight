// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "DifferenceCases.h"

#include <utility>

namespace an::tests::fixtures
{

namespace
{

constexpr const char* kGenerator = "src/AfterNightControls/controls/ComparisonImageGenerator.cpp";

DifferenceCase base(std::string operation, std::string input, std::string second, DifferenceMeasure measure,
                    std::string text, int line)
{
    DifferenceCase made;
    made.operation = std::move(operation);
    made.input = std::move(input);
    made.second = std::move(second);
    made.measure = measure;
    made.text = std::move(text);
    made.source = std::string(kGenerator) + ":" + std::to_string(line);
    return made;
}

std::vector<DifferenceCase> build()
{
    std::vector<DifferenceCase> cases;
    const std::string absolute = "the comparison view's difference: every channel's absolute difference";
    const std::string grey =
        "the comparison generator's grey difference: the mean of the channels' absolute differences";

    // Two floating scenes: the generator writes each difference into Float32 unchanged, as the operation does.
    cases.push_back(base("difference.absolute", "scene-rgb-float32", "scene-rgb-float32-second",
                         DifferenceMeasure::Absolute, absolute, 71));
    cases.push_back(base("difference.absolute", "scene-mono-float32", "scene-mono-float32-second",
                         DifferenceMeasure::Absolute, absolute, 71));

    // The grey mean: the generator multiplies the sum by the reciprocal of the channel count, the operation divides,
    // and the two roundings can part in the last bit of a double before the narrowing to Float32.
    DifferenceCase mean =
        base("difference.grey", "scene-rgb-float32", "scene-rgb-float32-second", DifferenceMeasure::Grey, grey, 59);
    mean.classification = ExpectationClass::Tolerance;
    mean.absoluteTolerance = 1.0e-7;
    mean.note = "a product by the reciprocal of three against a quotient by three, narrowed to Float32";
    cases.push_back(std::move(mean));

    // An integer image against itself: nothing differs, and zero is written as zero.
    cases.push_back(
        base("difference.absolute", "scene-rgb-uint16", "scene-rgb-uint16", DifferenceMeasure::Absolute, absolute, 71));

    // A signed integer image against itself. The record holds a fraction of full scale written back through an offset
    // convention for signed integers, so a zero difference lands on the lowest code.
    DifferenceCase signedSelf =
        base("difference.absolute", "scene-mono-int16", "scene-mono-int16", DifferenceMeasure::Absolute, absolute, 71);
    signedSelf.classification = ExpectationClass::Accepted;
    signedSelf.acceptedReason =
        "the record writes a difference into the first image's representation through its offset convention for "
        "signed integers, so a signed image's zero difference is its lowest code; the operation writes a Float32 "
        "difference, and zero is zero";
    cases.push_back(std::move(signedSelf));
    return cases;
}

} // namespace

const std::vector<DifferenceCase>& differenceCases()
{
    static const std::vector<DifferenceCase> cases = build();
    return cases;
}

const DifferenceCase* differenceCase(std::string_view id)
{
    for (const DifferenceCase& candidate : differenceCases())
    {
        if (candidate.id() == id)
        {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace an::tests::fixtures
